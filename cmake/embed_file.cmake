file(READ ${INPUT} content HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," content "${content}")
file(WRITE ${OUTPUT} "const unsigned char ${SYMBOL}[] = {${content}};\n")
