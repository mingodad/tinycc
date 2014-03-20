FNAME=tcc_attachments
./bin2c -m -d ${FNAME}.h -o ${FNAME}.c include/* win32/include/* \
    win32/include/sec_api/* win32/include/sec_api/sys/* \
    win32/include/sys/* win32/include/winapi/* libtcc.a libtcc1.a
gcc -c ${FNAME}.c
