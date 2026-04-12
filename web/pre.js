Module.preRun = Module.preRun || [];
Module.preRun.push(function() {
  document.addEventListener('keydown', function(e) {
    if (['Space','ArrowUp','ArrowDown','ArrowLeft','ArrowRight','Tab'].includes(e.code)) {
      e.preventDefault();
    }
  });
});
