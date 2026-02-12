cmake -S . -B build_opensource -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DTARGET_TYPE=OpenSource

echo ----------------------BUILD START------------------------
echo ---------------------------------------------------------
echo ---------------------------------------------------------
cmake --build build_opensource -j18