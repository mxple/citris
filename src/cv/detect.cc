#include "detect.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace imp {
namespace {

constexpr int kRows = Board::kVisibleHeight;
constexpr int kCols = Board::kWidth;
constexpr int kSkyRows = 3;
constexpr int kMaxQueue = 6;
constexpr int kPieceMinBboxDim = 8;
constexpr std::array<std::pair<int, int>, 5> kPieceFootprints = {{
    {1, 4}, {4, 1}, {2, 2}, {2, 3}, {3, 2},
}};

// =====================================================================
// Helpers
// =====================================================================

cv::Mat to_bgr(const CvImage& img) {
  cv::Mat rgba(img.h, img.w, CV_8UC4,
               const_cast<uint8_t*>(img.rgba.data()));
  cv::Mat bgr;
  cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
  return bgr;
}

// H:[0,360), S:[0,1], V:[0,1], float32 3-channel.
// Uses the uint8 HSV path then scales, matching the Python conversion:
//   hsv = cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV).astype(np.float32)
//   hsv[:,:,0] *= 2; hsv[:,:,1] /= 255; hsv[:,:,2] /= 255
cv::Mat to_hsv_float(const cv::Mat& bgr) {
  cv::Mat hsv_u8;
  cv::cvtColor(bgr, hsv_u8, cv::COLOR_BGR2HSV);
  std::vector<cv::Mat> chs(3);
  cv::split(hsv_u8, chs);
  cv::Mat H, S, V;
  chs[0].convertTo(H, CV_32F, 2.0);
  chs[1].convertTo(S, CV_32F, 1.0 / 255.0);
  chs[2].convertTo(V, CV_32F, 1.0 / 255.0);
  cv::Mat hsv;
  cv::merge(std::vector<cv::Mat>{H, S, V}, hsv);
  return hsv;
}

float percentile(const cv::Mat& m, int p) {
  std::vector<float> v(m.begin<float>(), m.end<float>());
  if (v.empty()) return 0.f;
  size_t k = std::min<size_t>(size_t(p) * (v.size() - 1) / 100, v.size() - 1);
  std::nth_element(v.begin(), v.begin() + k, v.end());
  return v[k];
}

float hue_dist(float a, float b) {
  float d = std::fabs(a - b);
  return d > 180.f ? 360.f - d : d;
}

int nearest_piece(float hue, const std::array<float, 7>& hues) {
  int best = 0;
  float bd = 360.f;
  for (int i = 0; i < 7; ++i) {
    float d = hue_dist(hue, hues[i]);
    if (d < bd) { bd = d; best = i; }
  }
  return best;
}

// =====================================================================
// Board finder (4-side scoring + corner termination)
// =====================================================================

struct Run { int start = 0, end = 0, length = 0; };

std::vector<Run> longest_run_per_column(const cv::Mat& mask) {
  int h = mask.rows, w = mask.cols;
  std::vector<Run> out(w);
  for (int x = 0; x < w; ++x) {
    int best_len = 0, best_start = 0, best_end = 0, cur = -1;
    for (int y = 0; y <= h; ++y) {
      bool on = y < h && mask.at<uint8_t>(y, x) != 0;
      if (on && cur < 0) {
        cur = y;
      } else if (!on && cur >= 0) {
        int len = y - cur;
        if (len > best_len) { best_len = len; best_start = cur; best_end = y; }
        cur = -1;
      }
    }
    out[x] = {best_start, best_end, best_len};
  }
  return out;
}

std::vector<Run> longest_run_per_row(const cv::Mat& mask) {
  int h = mask.rows, w = mask.cols;
  std::vector<Run> out(h);
  for (int y = 0; y < h; ++y) {
    int best_len = 0, best_start = 0, best_end = 0, cur = -1;
    const uint8_t* row = mask.ptr<uint8_t>(y);
    for (int x = 0; x <= w; ++x) {
      bool on = x < w && row[x] != 0;
      if (on && cur < 0) {
        cur = x;
      } else if (!on && cur >= 0) {
        int len = x - cur;
        if (len > best_len) { best_len = len; best_start = cur; best_end = x; }
        cur = -1;
      }
    }
    out[y] = {best_start, best_end, best_len};
  }
  return out;
}

std::optional<CvRect> find_board(const cv::Mat& bgr, const DetectParams& p) {
  int ih = bgr.rows, iw = bgr.cols;
  if (iw < 80 || ih < 80) return std::nullopt;

  cv::Mat gray;
  cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

  cv::Mat gf;
  gray.convertTo(gf, CV_32F);
  cv::Mat blur;
  cv::GaussianBlur(gf, blur, cv::Size(0, 0), p.sharpen_sigma);
  gf += p.sharpen_alpha * (gf - blur);
  cv::min(gf, 255.f, gf);
  cv::max(gf, 0.f, gf);

  cv::Mat sx, sy;
  cv::Sobel(gf, sx, CV_32F, 1, 0, 3);
  cv::Sobel(gf, sy, CV_32F, 0, 1, 3);
  sx = cv::abs(sx);
  sy = cv::abs(sy);

  // Directional NMS: keep sx only where it dominates sy, and vice versa.
  cv::Mat sx_v = cv::Mat::zeros(sx.size(), CV_32F);
  cv::Mat sy_h = cv::Mat::zeros(sy.size(), CV_32F);
  sx.copyTo(sx_v, sx > p.directional_ratio * sy);
  sy.copyTo(sy_h, sy > p.directional_ratio * sx);

  float v_thr = percentile(sx_v, p.edge_percentile);
  float h_thr = percentile(sy_h, p.edge_percentile);
  if (v_thr <= 0.f) return std::nullopt;

  cv::Mat vert_f, horiz_f;
  cv::threshold(sx_v, vert_f, v_thr, 255.0, cv::THRESH_BINARY);
  vert_f.convertTo(vert_f, CV_8U);
  if (h_thr > 0.f) {
    cv::threshold(sy_h, horiz_f, h_thr, 255.0, cv::THRESH_BINARY);
    horiz_f.convertTo(horiz_f, CV_8U);
  } else {
    horiz_f = cv::Mat::zeros(sy_h.size(), CV_8U);
  }

  cv::Mat vert, horiz;
  cv::morphologyEx(vert_f, vert, cv::MORPH_CLOSE,
                   cv::getStructuringElement(cv::MORPH_RECT, cv::Size(1, 5)));
  cv::morphologyEx(horiz_f, horiz, cv::MORPH_CLOSE,
                   cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 1)));

  cv::Mat vmask, hmask;
  vert.convertTo(vmask, CV_32F, 1.0 / 255.0);
  horiz.convertTo(hmask, CV_32F, 1.0 / 255.0);

  // v_cum: cumulative sum along Y, shape (ih+1, iw)
  cv::Mat v_cum = cv::Mat::zeros(ih + 1, iw, CV_32F);
  for (int y = 0; y < ih; ++y) {
    const float* vm = vmask.ptr<float>(y);
    const float* prev = v_cum.ptr<float>(y);
    float* next = v_cum.ptr<float>(y + 1);
    for (int x = 0; x < iw; ++x) next[x] = prev[x] + vm[x];
  }
  // h_cum: cumulative sum along X, shape (ih, iw+1)
  cv::Mat h_cum = cv::Mat::zeros(ih, iw + 1, CV_32F);
  for (int y = 0; y < ih; ++y) {
    const float* hm = hmask.ptr<float>(y);
    float* row = h_cum.ptr<float>(y);
    for (int x = 0; x < iw; ++x) row[x + 1] = row[x] + hm[x];
  }

  auto v_runs = longest_run_per_column(vert);
  auto h_runs = longest_run_per_row(horiz);

  int min_w = std::max(50, int(0.15f * std::min(iw, ih)));
  int max_w = (ih - 2) / 2;

  std::vector<int> cand_cols;
  for (int x = 0; x < iw; ++x)
    if (v_runs[x].length >= min_w / 2) cand_cols.push_back(x);
  if (cand_cols.empty()) return std::nullopt;
  std::sort(cand_cols.begin(), cand_cols.end(),
            [&](int a, int b) { return v_runs[a].length > v_runs[b].length; });
  if (cand_cols.size() > 80) cand_cols.resize(80);

  std::vector<int> cand_rows;
  for (int y = 3; y <= ih - 4; ++y)
    if (h_runs[y].length >= min_w / 2) cand_rows.push_back(y);
  if (cand_rows.empty()) return std::nullopt;
  std::sort(cand_rows.begin(), cand_rows.end(),
            [&](int a, int b) { return h_runs[a].length > h_runs[b].length; });
  if (cand_rows.size() > 80) cand_rows.resize(80);

  // Bounds-safe cumsum accessors
  auto vc = [&](int y, int x) -> float {
    return v_cum.at<float>(std::clamp(y, 0, ih), std::clamp(x, 0, iw - 1));
  };
  auto hc = [&](int y, int x) -> float {
    return h_cum.at<float>(std::clamp(y, 0, ih - 1), std::clamp(x, 0, iw));
  };

  float best_score = -1.f;
  std::optional<CvRect> best;

  for (int L : cand_cols) {
    for (int R : cand_cols) {
      int W = R - L;
      if (W < min_w || W > max_w) continue;

      float pitch = W / 10.f;
      int wc = std::max(3, W / 30);
      int Lo = std::max(0, L - wc), Li = std::min(iw, L + wc);
      int Ri = std::max(0, R - wc), Ro = std::min(iw, R + wc);

      for (int T : cand_rows) {
        for (int B : cand_rows) {
          int H = B - T;
          if (H <= 0 || H < int(1.92f * W) || H > int(2.08f * W)) continue;

          float Hf = float(H);
          float ls = (vc(B, L) - vc(T, L)) / Hf;
          float rs = (vc(B, R) - vc(T, R)) / Hf;
          float ts = (hc(T, R) - hc(T, L)) / float(W);
          float bs = (hc(B, R) - hc(B, L)) / float(W);
          float cov = ls + rs + ts + bs;
          if (cov < 1.5f) continue;

          // Corner termination
          auto term_h_fn = [&](int y, int xa, int xb, int oa, int ob) {
            float in_v = (xb > xa) ? (hc(y, xb) - hc(y, xa)) / float(xb - xa) : 0.f;
            float ou_v = (ob > oa) ? (hc(y, ob) - hc(y, oa)) / float(ob - oa) : 0.f;
            return std::clamp(in_v - ou_v, 0.f, 1.f);
          };
          auto term_v_fn = [&](int x, int ya, int yb, int oa, int ob) {
            float in_v = (yb > ya) ? (vc(yb, x) - vc(ya, x)) / float(yb - ya) : 0.f;
            float ou_v = (ob > oa) ? (vc(ob, x) - vc(oa, x)) / float(ob - oa) : 0.f;
            return std::clamp(in_v - ou_v, 0.f, 1.f);
          };

          int tib = std::min(ih, T + wc), toa = std::max(0, T - wc);
          int bia = std::max(0, B - wc), bob = std::min(ih, B + wc);

          float tl = term_h_fn(T, L, Li, Lo, L) * term_v_fn(L, T, tib, toa, T);
          float tr = term_h_fn(T, Ri, R, R, Ro) * term_v_fn(R, T, tib, toa, T);
          float bl = term_h_fn(B, L, Li, Lo, L) * term_v_fn(L, bia, B, B, bob);
          float br = term_h_fn(B, Ri, R, R, Ro) * term_v_fn(R, bia, B, B, bob);
          float corner = (tl + tr + bl + br) / 4.f;

          // Grid alignment: internal column edge density
          float grid_v = 0.f;
          for (int k = 1; k < 10; ++k) {
            int c = std::clamp(int(std::round(L + k * pitch)), 0, iw - 1);
            grid_v += (vc(B, c) - vc(T, c)) / Hf;
          }
          grid_v /= 9.f;

          float score = cov * std::sqrt(float(W) * Hf) *
                        (1.f + 3.f * grid_v) *
                        (0.5f + 1.5f * corner);
          if (score > best_score) {
            best_score = score;
            best = CvRect{L, T, W, H};
          }
        }
      }
    }
  }

  return best;
}

// =====================================================================
// Hold/queue shape detection with stack constraints
// =====================================================================

struct PieceBlob { int x, y, w, h, area; };

std::vector<PieceBlob> find_piece_blobs(const cv::Mat& bgr, float pitch,
                                         const DetectParams& p) {
  cv::Mat hsv = to_hsv_float(bgr);
  std::vector<cv::Mat> chs;
  cv::split(hsv, chs);

  cv::Mat mask;
  cv::bitwise_and(chs[1] > p.piece_sat_thresh,
                  chs[2] > p.piece_val_thresh, mask);

  if (p.piece_dilate_px > 1) {
    cv::dilate(mask, mask, cv::getStructuringElement(
        cv::MORPH_RECT, cv::Size(p.piece_dilate_px, p.piece_dilate_px)));
  }

  int min_dim = kPieceMinBboxDim;
  float min_cell = 0.f;
  if (pitch > 0.f) {
    min_dim = std::max(kPieceMinBboxDim,
                       int(std::round(p.piece_min_bbox_pitch_frac * pitch)));
    min_cell = p.piece_min_cell_pitch_frac * pitch;
  }

  cv::Mat labels, stats, centroids;
  int n = cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8);

  std::vector<PieceBlob> results;
  for (int i = 1; i < n; ++i) {
    int bx = stats.at<int>(i, cv::CC_STAT_LEFT);
    int by = stats.at<int>(i, cv::CC_STAT_TOP);
    int bw = stats.at<int>(i, cv::CC_STAT_WIDTH);
    int bh = stats.at<int>(i, cv::CC_STAT_HEIGHT);
    int area = stats.at<int>(i, cv::CC_STAT_AREA);
    if (bw < min_dim || bh < min_dim) continue;

    for (auto [cw, ch] : kPieceFootprints) {
      float cell_w = float(bw) / cw;
      float cell_h = float(bh) / ch;
      float maxc = std::max(cell_w, cell_h);
      if (maxc <= 0.f) continue;
      if (std::fabs(cell_w - cell_h) / maxc > p.piece_pitch_agree) continue;
      if (std::min(cell_w, cell_h) < min_cell) continue;

      cv::Mat blob_roi = (labels(cv::Rect(bx, by, bw, bh)) == i);
      cv::Mat blob_f;
      blob_roi.convertTo(blob_f, CV_32F, 1.0 / 255.0);
      cv::Mat small;
      cv::resize(blob_f, small, cv::Size(cw, ch), 0, 0, cv::INTER_AREA);
      int filled = cv::countNonZero(small > 0.5f);
      if (std::abs(filled - 4) <= p.piece_fill_tol) {
        results.push_back({bx, by, bw, bh, area});
        break;
      }
    }
  }
  return results;
}

void find_hold_and_queue(const cv::Mat& bgr, CvRect board, float pitch,
                          std::optional<CvRect>& hold_out,
                          std::vector<CvRect>& queue_out,
                          const DetectParams& p) {
  hold_out.reset();
  queue_out.clear();

  auto blobs = find_piece_blobs(bgr, 0.f, p);
  float band = pitch * 5.f;
  float upper_y_max = board.y + board.h * p.hold_y_max_board_frac;

  // Hold: largest blob (by bbox area) in left band, y-center in upper portion
  const PieceBlob* best_hold = nullptr;
  for (const auto& b : blobs) {
    if (b.x + b.w > board.x) continue;
    if (float(board.x) - (float(b.x) + float(b.w) / 2.f) > band) continue;
    if (float(b.y) + float(b.h) / 2.f > upper_y_max) continue;
    if (!best_hold || b.w * b.h > best_hold->w * best_hold->h) best_hold = &b;
  }
  if (best_hold)
    hold_out = CvRect{best_hold->x, best_hold->y, best_hold->w, best_hold->h};

  // Queue: blobs right of board, x-clustered, stack-constrained
  float queue_y_max = board.y + board.h * p.queue_y_max_board_frac;
  std::vector<const PieceBlob*> right;
  for (const auto& b : blobs) {
    if (b.x < board.x + board.w) continue;
    if ((float(b.x) + float(b.w) / 2.f) - float(board.x + board.w) > band) continue;
    if (float(b.y) + float(b.h) / 2.f > queue_y_max) continue;
    right.push_back(&b);
  }
  if (right.empty()) return;

  // Densest center-x cluster (radius = pitch)
  std::vector<float> cxs;
  for (auto* b : right) cxs.push_back(float(b->x) + float(b->w) / 2.f);
  int best_cnt = 0;
  float anchor = cxs[0];
  for (float c : cxs) {
    int cnt = 0;
    for (float c2 : cxs) if (std::fabs(c2 - c) <= pitch) ++cnt;
    if (cnt > best_cnt) { best_cnt = cnt; anchor = c; }
  }
  std::vector<const PieceBlob*> clustered;
  for (auto* b : right)
    if (std::fabs((float(b->x) + float(b->w) / 2.f) - anchor) <= pitch)
      clustered.push_back(b);
  std::sort(clustered.begin(), clustered.end(),
            [](const PieceBlob* a, const PieceBlob* b) { return a->y < b->y; });

  // Overlap dedup: walk top→bottom, keep larger blob (by bbox area) on overlap
  int eps = std::max(2, int(pitch * p.queue_stack_overlap_eps_frac));
  std::vector<const PieceBlob*> stacked;
  for (auto* b : clustered) {
    if (!stacked.empty() && b->y < stacked.back()->y + stacked.back()->h - eps) {
      if (b->w * b->h > stacked.back()->w * stacked.back()->h)
        stacked.back() = b;
      continue;
    }
    stacked.push_back(b);
  }

  // Area consistency: drop blobs much smaller than median (by bbox area)
  if (stacked.size() >= 3) {
    std::vector<int> areas;
    for (auto* b : stacked) areas.push_back(b->w * b->h);
    std::sort(areas.begin(), areas.end());
    int med = areas[areas.size() / 2];
    std::erase_if(stacked, [&](const PieceBlob* b) {
      return b->w * b->h < int(p.queue_area_consistency_frac * float(med));
    });
  }

  // Gap padding: insert empty CvRect for missed slots so piece indices stay correct
  if (!stacked.empty()) {
    std::vector<float> cys;
    for (auto* b : stacked)
      cys.push_back(float(b->y) + float(b->h) / 2.f);
    float stride;
    if (cys.size() >= 2) {
      std::vector<float> strides;
      for (size_t i = 0; i + 1 < cys.size(); ++i)
        strides.push_back(cys[i + 1] - cys[i]);
      std::sort(strides.begin(), strides.end());
      stride = strides[strides.size() / 2];
    } else {
      stride = pitch * 3.f;
    }
    queue_out.push_back({stacked[0]->x, stacked[0]->y,
                         stacked[0]->w, stacked[0]->h});
    for (size_t i = 1; i < stacked.size(); ++i) {
      float gap = (float(stacked[i]->y) + float(stacked[i]->h) / 2.f) -
                  (float(stacked[i-1]->y) + float(stacked[i-1]->h) / 2.f);
      if (stride > 0.f && gap > p.queue_stride_gap_factor * stride) {
        int missed = std::max(0, int(std::round(gap / stride)) - 1);
        for (int m = 0; m < missed && int(queue_out.size()) < kMaxQueue; ++m)
          queue_out.push_back({0, 0, 0, 0});
      }
      if (int(queue_out.size()) >= kMaxQueue) break;
      queue_out.push_back({stacked[i]->x, stacked[i]->y,
                           stacked[i]->w, stacked[i]->h});
    }
    if (queue_out.size() > size_t(kMaxQueue))
      queue_out.resize(kMaxQueue);
  }
}

// =====================================================================
// Cell classification
// =====================================================================

CellColor classify_cell(float hue, float sat, float val, float fill,
                          const std::array<float, 7>& hues, float empty_v,
                          const DetectParams& p) {
  float score = val * (1.f - p.cell_sat_weight) + sat * p.cell_sat_weight;
  if (fill < p.cell_fill_frac || score < p.cell_thresh) return CellColor::Empty;
  if (sat < p.cell_grey_sat) return CellColor::Garbage;
  if (val < p.cell_empty_val) return CellColor::Garbage;
  return static_cast<CellColor>(nearest_piece(hue, hues) + 1);
}

struct GridStats { cv::Mat H, S, V, F; };

GridStats sample_grid_hsv(const cv::Mat& bgr, CvRect board,
                           int cols, int rows, const DetectParams& p) {
  GridStats gs;
  gs.H = cv::Mat::zeros(rows, cols, CV_32F);
  gs.S = cv::Mat::zeros(rows, cols, CV_32F);
  gs.V = cv::Mat::zeros(rows, cols, CV_32F);
  gs.F = cv::Mat::zeros(rows, cols, CV_32F);

  cv::Mat hsv = to_hsv_float(bgr);
  int ih = bgr.rows, iw = bgr.cols;
  float cw = board.w / float(cols);
  float ch = board.h / float(rows);
  int half = std::max(3, int(std::round(std::min(cw, ch) * 0.20f)));

  std::vector<float> hs, ss, vs;
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      int cx = int(board.x + (c + 0.5f) * cw);
      int cy = int(board.y + (r + 0.5f) * ch);
      int x0 = std::max(0, cx - half), x1 = std::min(iw, cx + half + 1);
      int y0 = std::max(0, cy - half), y1 = std::min(ih, cy + half + 1);
      if (x1 <= x0 || y1 <= y0) continue;

      hs.clear(); ss.clear(); vs.clear();
      int total = 0, bright = 0;
      for (int y = y0; y < y1; ++y) {
        const cv::Vec3f* row = hsv.ptr<cv::Vec3f>(y);
        for (int x = x0; x < x1; ++x) {
          ++total;
          float v = row[x][2], s = row[x][1];
          float score = v * (1.f - p.cell_sat_weight) + s * p.cell_sat_weight;
          if (score > p.cell_thresh) {
            ++bright;
            hs.push_back(row[x][0]);
            ss.push_back(s);
            vs.push_back(v);
          }
        }
      }
      if (total == 0) continue;
      gs.F.at<float>(r, c) = float(bright) / float(total);
      if (hs.empty()) continue;
      auto med = [](std::vector<float>& v) {
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        return v[v.size() / 2];
      };
      gs.H.at<float>(r, c) = med(hs);
      gs.S.at<float>(r, c) = med(ss);
      gs.V.at<float>(r, c) = med(vs);
    }
  }
  return gs;
}

float calibrate_empty_v(const cv::Mat& bgr, CvRect board, const DetectParams& p) {
  int ih = bgr.rows, iw = bgr.cols;
  float cell_w = board.w / 10.f;
  float cell_h = board.h / 20.f;
  int tw = std::max(1, int(std::round(2 * cell_w)));
  int th = std::max(1, int(std::round(2 * cell_h)));

  std::vector<float> samples;
  for (int x_left : {board.x, board.x + board.w - tw}) {
    int x0 = std::max(0, x_left), x1 = std::min(iw, x0 + tw);
    int y0 = std::max(0, board.y), y1 = std::min(ih, board.y + th);
    if (x1 <= x0 || y1 <= y0) continue;
    cv::Mat roi = bgr(cv::Rect(x0, y0, x1 - x0, y1 - y0));
    cv::Mat hsv_u8;
    cv::cvtColor(roi, hsv_u8, cv::COLOR_BGR2HSV);
    for (int y = 0; y < hsv_u8.rows; ++y) {
      const cv::Vec3b* row = hsv_u8.ptr<cv::Vec3b>(y);
      for (int x = 0; x < hsv_u8.cols; ++x)
        samples.push_back(float(row[x][2]) / 255.f);
    }
  }
  if (samples.empty()) return p.cell_empty_val;

  size_t k = std::max<size_t>(0, size_t(0.1f * float(samples.size() - 1)));
  std::nth_element(samples.begin(), samples.begin() + k, samples.end());
  float bg = samples[k];
  return std::clamp(bg + p.empty_v_margin, p.empty_v_min, p.empty_v_max);
}

std::array<float, 7> discover_piece_hues(const cv::Mat& H, const cv::Mat& S,
                                           const cv::Mat& V, const cv::Mat& F,
                                           const DetectParams& p) {
  std::array<float, 7> table = p.piece_hues;

  std::vector<float> hues;
  for (int r = 0; r < H.rows; ++r)
    for (int c = 0; c < H.cols; ++c)
      if (F.at<float>(r, c) >= p.cell_fill_frac &&
          V.at<float>(r, c) >= p.cell_empty_val &&
          S.at<float>(r, c) >= p.cell_grey_sat)
        hues.push_back(H.at<float>(r, c));
  if (int(hues.size()) < p.hue_cluster_min_hues) return table;

  // 36-bin circular histogram, 10 deg/bin
  constexpr int kBins = 36;
  constexpr float kBinW = 360.f / kBins;
  std::array<float, kBins> hist{};
  for (float h : hues)
    hist[std::clamp(int(h / kBinW), 0, kBins - 1)] += 1.f;

  // Circular Gaussian smooth (sigma=1.5 bins)
  constexpr int kHalf = 4;
  constexpr float kSigma = 1.5f;
  std::array<float, 2 * kHalf + 1> kern{};
  float ksum = 0.f;
  for (int i = 0; i < 2 * kHalf + 1; ++i) {
    float x = float(i - kHalf);
    kern[i] = std::exp(-(x * x) / (2.f * kSigma * kSigma));
    ksum += kern[i];
  }
  for (auto& k : kern) k /= ksum;

  std::array<float, kBins> smoothed{};
  for (int i = 0; i < kBins; ++i)
    for (int j = 0; j < 2 * kHalf + 1; ++j)
      smoothed[i] += hist[((i - kHalf + j) % kBins + kBins) % kBins] * kern[j];

  // Local maxima above threshold
  float thr = p.hue_cluster_min_frac * float(hues.size());
  struct Peak { float hue, height; };
  std::vector<Peak> peaks;
  for (int i = 0; i < kBins; ++i) {
    float cur = smoothed[i];
    float prev = smoothed[(i - 1 + kBins) % kBins];
    float next = smoothed[(i + 1) % kBins];
    if (cur >= prev && cur >= next && cur >= thr) {
      float denom = prev - 2.f * cur + next;
      float d = (std::fabs(denom) > 1e-9f)
                    ? std::clamp(0.5f * (prev - next) / denom, -0.5f, 0.5f)
                    : 0.f;
      peaks.push_back({std::fmod((i + d) * kBinW + kBinW / 2.f + 360.f, 360.f), cur});
    }
  }
  if (peaks.empty()) return table;
  std::sort(peaks.begin(), peaks.end(),
            [](const Peak& a, const Peak& b) { return a.height > b.height; });
  if (peaks.size() > 7) peaks.resize(7);

  // Greedy snap to nearest canonical piece hue
  std::array<bool, 7> claimed{};
  for (const auto& pk : peaks) {
    int best_i = -1;
    float best_d = p.piece_snap_max_deg + 1.f;
    for (int i = 0; i < 7; ++i) {
      if (claimed[i]) continue;
      float d = hue_dist(pk.hue, p.piece_hues[i]);
      if (d < best_d) { best_d = d; best_i = i; }
    }
    if (best_i >= 0 && best_d <= p.piece_snap_max_deg) {
      table[best_i] = pk.hue;
      claimed[best_i] = true;
    }
  }
  return table;
}

// =====================================================================
// CC grid sampler (Otsu + connected components + garbage rescue)
// =====================================================================

struct BlobInfo {
  std::vector<std::pair<int, int>> cells;
  std::vector<CellColor> colors;
};

Detection::Grid sample_grid_cc(const cv::Mat& bgr, CvRect board,
                                std::vector<BlobInfo>& blobs_out,
                                std::array<float, 7>& hue_table_out,
                                float& empty_v_out,
                                const DetectParams& p) {
  blobs_out.clear();

  auto gs = sample_grid_hsv(bgr, board, kCols, kRows, p);
  float empty_v = calibrate_empty_v(bgr, board, p);
  auto hue_table = discover_piece_hues(gs.H, gs.S, gs.V, gs.F, p);
  empty_v_out = empty_v;
  hue_table_out = hue_table;

  Detection::Grid grid{};
  for (int r = 0; r < kRows; ++r)
    for (int c = 0; c < kCols; ++c)
      grid[r][c] = classify_cell(gs.H.at<float>(r, c), gs.S.at<float>(r, c),
                                  gs.V.at<float>(r, c), gs.F.at<float>(r, c),
                                  hue_table, empty_v, p);

  // CC rescue pass: find dim garbage missed by per-cell HSV
  int ih = bgr.rows, iw = bgr.cols;
  int x0 = std::max(0, board.x), y0 = std::max(0, board.y);
  int x1 = std::min(iw, board.x + board.w);
  int y1 = std::min(ih, board.y + board.h);
  if (x1 <= x0 || y1 <= y0) return grid;

  cv::Mat roi = bgr(cv::Rect(x0, y0, x1 - x0, y1 - y0));
  int rh = roi.rows, rw = roi.cols;

  cv::Mat roi_hsv_f = to_hsv_float(roi);
  std::vector<cv::Mat> roi_chs;
  cv::split(roi_hsv_f, roi_chs);

  // Otsu on V channel → occupied mask
  cv::Mat V8;
  roi_chs[2].convertTo(V8, CV_8U, 255.0);
  cv::Mat dummy;
  double otsu_t = cv::threshold(V8, dummy, 0, 255,
                                 cv::THRESH_BINARY | cv::THRESH_OTSU);
  float thr_v = std::clamp(float(otsu_t / 255.0) - p.blob_v_otsu_backoff,
                            p.blob_v_min_thr, p.blob_v_max_thr);
  cv::Mat occ_mask;
  cv::threshold(V8, occ_mask, int(thr_v * 255.f), 255, cv::THRESH_BINARY);

  cv::Mat labels, stats, centroids;
  int n_cc = cv::connectedComponentsWithStats(occ_mask, labels, stats, centroids, 8);

  float cell_w = float(rw) / kCols;
  float cell_h = float(rh) / kRows;
  float cell_area = cell_w * cell_h;
  int min_area = std::max(4, int(cell_area * p.blob_min_area_frac));

  for (int bi = 1; bi < n_cc; ++bi) {
    if (stats.at<int>(bi, cv::CC_STAT_AREA) < min_area) continue;

    // Per-cell pixel count for this blob
    std::array<std::array<int, kCols>, kRows> ccnt{};
    for (int y = 0; y < rh; ++y) {
      const int* lrow = labels.ptr<int>(y);
      for (int x = 0; x < rw; ++x) {
        if (lrow[x] != bi) continue;
        int cr = std::clamp(int(y / cell_h), 0, kRows - 1);
        int cc = std::clamp(int(x / cell_w), 0, kCols - 1);
        ccnt[cr][cc]++;
      }
    }

    std::vector<std::pair<int, int>> assigned;
    for (int r = 0; r < kRows; ++r)
      for (int c = 0; c < kCols; ++c)
        if (float(ccnt[r][c]) / cell_area >= p.blob_cell_overlap)
          assigned.push_back({r, c});
    if (int(assigned.size()) < p.blob_min_cells) continue;

    BlobInfo info;
    for (auto [r, c] : assigned) {
      int cy0 = std::clamp(int(std::round(r * cell_h)), 0, rh);
      int cy1 = std::clamp(int(std::round((r + 1) * cell_h)), 0, rh);
      int cx0 = std::clamp(int(std::round(c * cell_w)), 0, rw);
      int cx1 = std::clamp(int(std::round((c + 1) * cell_w)), 0, rw);

      std::vector<float> s_vals, h_vals;
      for (int y = cy0; y < cy1; ++y) {
        const int* lrow = labels.ptr<int>(y);
        const float* hrow = roi_chs[0].ptr<float>(y);
        const float* srow = roi_chs[1].ptr<float>(y);
        for (int x = cx0; x < cx1; ++x) {
          if (lrow[x] != bi) continue;
          s_vals.push_back(srow[x]);
          h_vals.push_back(hrow[x]);
        }
      }
      if (s_vals.empty()) continue;

      // Build sat-filtered hue list BEFORE nth_element reorders s_vals
      std::vector<float> h_sat;
      for (size_t i = 0; i < s_vals.size(); ++i)
        if (s_vals[i] > p.blob_s_thr) h_sat.push_back(h_vals[i]);

      std::nth_element(s_vals.begin(), s_vals.begin() + s_vals.size() / 2,
                       s_vals.end());
      float s_med = s_vals[s_vals.size() / 2];

      CellColor color;
      if (s_med < p.cell_grey_sat) {
        color = CellColor::Garbage;
      } else {
        auto& h_use = h_sat.empty() ? h_vals : h_sat;
        std::nth_element(h_use.begin(), h_use.begin() + h_use.size() / 2,
                         h_use.end());
        color = static_cast<CellColor>(
            nearest_piece(h_use[h_use.size() / 2], hue_table) + 1);
      }

      info.cells.push_back({r, c});
      info.colors.push_back(color);
    }
    if (!info.cells.empty()) blobs_out.push_back(std::move(info));
  }

  return grid;
}

// =====================================================================
// Sky scan + current piece detection
// =====================================================================

using FlatGrid = std::vector<std::array<CellColor, kCols>>;

FlatGrid sample_above_board(const cv::Mat& bgr, CvRect board,
                              const std::array<float, 7>& hues, float empty_v,
                              const DetectParams& p) {
  FlatGrid sky(kSkyRows);
  for (auto& row : sky) row.fill(CellColor::Empty);

  float ch = board.h / 20.f;
  int sky_top = std::max(0, int(std::round(board.y - kSkyRows * ch)));
  int sky_h = board.y - sky_top;
  if (sky_h < 1) return sky;

  int actual = std::max(1, std::min(kSkyRows, int(std::round(sky_h / ch))));
  CvRect sky_board{board.x, sky_top, board.w, int(std::round(actual * ch))};
  auto gs = sample_grid_hsv(bgr, sky_board, kCols, actual, p);

  int off = kSkyRows - actual;
  for (int r = 0; r < actual; ++r)
    for (int c = 0; c < kCols; ++c)
      sky[off + r][c] = classify_cell(
          gs.H.at<float>(r, c), gs.S.at<float>(r, c),
          gs.V.at<float>(r, c), gs.F.at<float>(r, c), hues, empty_v, p);
  return sky;
}

struct CurrentResult {
  PieceType piece;
  std::vector<std::pair<int, int>> cells;
};

// 4-connected floodfill: find unique floating 4-cell mono-color component.
std::optional<CurrentResult> find_floating_tetromino(
    const FlatGrid& grid, int row_offset = 0) {
  int rows = int(grid.size());
  std::vector<std::vector<bool>> visited(rows, std::vector<bool>(kCols, false));
  std::vector<CurrentResult> candidates;
  std::vector<std::pair<int, int>> stk;

  constexpr std::array<std::pair<int, int>, 4> kDirs = {{{1,0},{-1,0},{0,1},{0,-1}}};

  for (int r0 = 0; r0 < rows; ++r0) {
    for (int c0 = 0; c0 < kCols; ++c0) {
      if (visited[r0][c0]) continue;
      CellColor col = grid[r0][c0];
      if (col == CellColor::Empty || col == CellColor::Garbage) continue;

      std::vector<std::pair<int, int>> cells;
      stk.clear();
      stk.push_back({r0, c0});
      visited[r0][c0] = true;
      while (!stk.empty()) {
        auto [r, c] = stk.back(); stk.pop_back();
        cells.push_back({r, c});
        for (auto [dr, dc] : kDirs) {
          int nr = r + dr, nc = c + dc;
          if (nr < 0 || nr >= rows || nc < 0 || nc >= kCols) continue;
          if (visited[nr][nc] || grid[nr][nc] != col) continue;
          visited[nr][nc] = true;
          stk.push_back({nr, nc});
        }
      }
      if (cells.size() != 4) continue;

      bool floating = true;
      for (auto [r, c] : cells) {
        for (auto [dr, dc] : kDirs) {
          int nr = r + dr, nc = c + dc;
          bool internal = false;
          for (auto [pr, pc] : cells)
            if (pr == nr && pc == nc) { internal = true; break; }
          if (internal) continue;
          if (nr < 0 || nr >= rows || nc < 0 || nc >= kCols) continue;
          if (grid[nr][nc] != CellColor::Empty) { floating = false; break; }
        }
        if (!floating) break;
      }
      if (!floating) continue;

      auto pt = static_cast<PieceType>(static_cast<int>(col) - 1);
      std::vector<std::pair<int, int>> shifted;
      for (auto [r, c] : cells) shifted.push_back({r + row_offset, c});
      candidates.push_back({pt, std::move(shifted)});
    }
  }
  return (candidates.size() == 1) ? std::optional(candidates[0]) : std::nullopt;
}

std::optional<CurrentResult> detect_current_with_sky(
    const FlatGrid& sky, const Detection::Grid& grid) {
  FlatGrid ext;
  ext.insert(ext.end(), sky.begin(), sky.end());
  for (const auto& row : grid) ext.push_back(row);
  return find_floating_tetromino(ext, -kSkyRows);
}

std::optional<CurrentResult> detect_current_playfield(
    const Detection::Grid& grid) {
  FlatGrid flat(grid.begin(), grid.end());
  return find_floating_tetromino(flat);
}

std::optional<CurrentResult> detect_current_from_blobs(
    const Detection::Grid& grid, const std::vector<BlobInfo>& blobs) {
  constexpr std::array<std::pair<int, int>, 4> kDirs = {{{1,0},{-1,0},{0,1},{0,-1}}};
  std::vector<CurrentResult> candidates;

  for (const auto& blob : blobs) {
    if (blob.cells.size() != 4) continue;
    CellColor first = blob.colors[0];
    if (first == CellColor::Empty || first == CellColor::Garbage) continue;
    bool uniform = true;
    for (size_t i = 1; i < blob.colors.size(); ++i)
      if (blob.colors[i] != first) { uniform = false; break; }
    if (!uniform) continue;

    bool floating = true;
    for (auto [r, c] : blob.cells) {
      for (auto [dr, dc] : kDirs) {
        int nr = r + dr, nc = c + dc;
        bool internal = false;
        for (auto [pr, pc] : blob.cells)
          if (pr == nr && pc == nc) { internal = true; break; }
        if (internal) continue;
        if (nr >= kRows) { floating = false; break; }
        if (nr >= 0 && nr < kRows && nc >= 0 && nc < kCols &&
            grid[nr][nc] != CellColor::Empty) {
          floating = false; break;
        }
      }
      if (!floating) break;
    }
    if (!floating) continue;

    auto pt = static_cast<PieceType>(static_cast<int>(first) - 1);
    candidates.push_back({pt, blob.cells});
  }
  return (candidates.size() == 1) ? std::optional(candidates[0]) : std::nullopt;
}

constexpr int kPartialMaxRow = 1;

std::optional<CurrentResult> detect_current_partial(
    const FlatGrid& sky, const Detection::Grid& playfield) {
  FlatGrid ext;
  ext.insert(ext.end(), sky.begin(), sky.end());
  for (const auto& row : playfield) ext.push_back(row);

  int total_rows = int(ext.size());
  int max_row = kSkyRows + kPartialMaxRow;

  std::vector<std::vector<bool>> visited(total_rows, std::vector<bool>(kCols, false));
  std::vector<CurrentResult> candidates;
  std::vector<std::pair<int, int>> stk;
  constexpr std::array<std::pair<int, int>, 4> kDirs = {{{1,0},{-1,0},{0,1},{0,-1}}};

  // Seed only from playfield rows to avoid UI noise in sky
  for (int r0 = kSkyRows; r0 <= std::min(total_rows - 1, max_row); ++r0) {
    for (int c0 = 0; c0 < kCols; ++c0) {
      if (visited[r0][c0]) continue;
      CellColor col = ext[r0][c0];
      if (col == CellColor::Empty || col == CellColor::Garbage) continue;

      std::vector<std::pair<int, int>> cells;
      stk.clear();
      stk.push_back({r0, c0});
      while (!stk.empty()) {
        auto [r, c] = stk.back(); stk.pop_back();
        if (r < 0 || r >= total_rows || c < 0 || c >= kCols) continue;
        if (visited[r][c] || ext[r][c] != col) continue;
        visited[r][c] = true;
        cells.push_back({r, c});
        for (auto [dr, dc] : kDirs) stk.push_back({r + dr, c + dc});
      }
      if (cells.size() < 2 || cells.size() > 3) continue;

      bool in_range = true;
      for (auto [r, c] : cells)
        if (r > max_row) { in_range = false; break; }
      if (!in_range) continue;

      bool floating = true;
      for (auto [r, c] : cells) {
        for (auto [dr, dc] : kDirs) {
          int nr = r + dr, nc = c + dc;
          bool internal = false;
          for (auto [pr, pc] : cells)
            if (pr == nr && pc == nc) { internal = true; break; }
          if (internal) continue;
          if (nr >= 0 && nr < total_rows && nc >= 0 && nc < kCols &&
              ext[nr][nc] != CellColor::Empty) {
            floating = false; break;
          }
        }
        if (!floating) break;
      }
      if (!floating) continue;

      auto pt = static_cast<PieceType>(static_cast<int>(col) - 1);
      std::vector<std::pair<int, int>> shifted;
      for (auto [r, c] : cells) shifted.push_back({r - kSkyRows, c});
      candidates.push_back({pt, std::move(shifted)});
    }
  }
  return (candidates.size() == 1) ? std::optional(candidates[0]) : std::nullopt;
}

// =====================================================================
// Piece type classification from bbox
// =====================================================================

std::optional<PieceType> classify_piece_bbox(
    const cv::Mat& bgr, CvRect r,
    const std::array<float, 7>& hues, const DetectParams& p) {
  if (r.w <= 0 || r.h <= 0) return std::nullopt;
  int x0 = std::max(0, r.x), y0 = std::max(0, r.y);
  int x1 = std::min(bgr.cols, r.x + r.w);
  int y1 = std::min(bgr.rows, r.y + r.h);
  if (x1 <= x0 || y1 <= y0) return std::nullopt;

  cv::Mat hsv = to_hsv_float(bgr(cv::Rect(x0, y0, x1 - x0, y1 - y0)));
  std::vector<float> hs;
  for (int y = 0; y < hsv.rows; ++y) {
    const cv::Vec3f* row = hsv.ptr<cv::Vec3f>(y);
    for (int x = 0; x < hsv.cols; ++x) {
      if (row[x][2] <= p.cell_thresh || row[x][1] <= p.cell_grey_sat) continue;
      hs.push_back(row[x][0]);
    }
  }
  if (hs.empty()) return std::nullopt;
  std::nth_element(hs.begin(), hs.begin() + hs.size() / 2, hs.end());
  return static_cast<PieceType>(nearest_piece(hs[hs.size() / 2], hues));
}

}  // namespace

// =====================================================================
// Top-level pipeline
// =====================================================================

std::optional<Detection> detect(const CvImage& img, const DetectParams& p) {
  if (img.empty()) return std::nullopt;
  cv::Mat bgr = to_bgr(img);

  auto board = find_board(bgr, p);
  if (!board) return std::nullopt;

  Detection det;
  det.board = *board;
  det.pitch = (board->w / 10.f + board->h / 20.f) / 2.f;

  find_hold_and_queue(bgr, det.board, det.pitch, det.hold, det.queue, p);

  std::vector<BlobInfo> blobs;
  std::array<float, 7> hue_table;
  float empty_v;
  det.grid = sample_grid_cc(bgr, det.board, blobs, hue_table, empty_v, p);

  auto sky = sample_above_board(bgr, det.board, hue_table, empty_v, p);

  auto current = detect_current_with_sky(sky, det.grid);
  if (!current) current = detect_current_playfield(det.grid);
  if (!current) current = detect_current_from_blobs(det.grid, blobs);
  if (!current) current = detect_current_partial(sky, det.grid);

  if (current) {
    det.current_piece = current->piece;
    det.current_cells = std::move(current->cells);
  }

  if (det.hold) det.hold_piece = classify_piece_bbox(bgr, *det.hold, hue_table, p);
  for (const auto& q : det.queue)
    if (auto piece = classify_piece_bbox(bgr, q, hue_table, p))
      det.queue_pieces.push_back(*piece);

  return det;
}

}  // namespace imp
