file(READ "${FILE}" content)
string(REGEX REPLACE "(^|\n)unsigned" "\\1inline unsigned" content "${content}")
file(WRITE "${FILE}" "${content}")
