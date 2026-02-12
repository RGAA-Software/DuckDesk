cmake -S . -B build_official -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_TYPE=Official

echo ----------------------BUILD START------------------------
echo ---------------------------------------------------------
echo ---------------------------------------------------------
cmake --build build_official -j18