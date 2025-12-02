g++ -std=c++17 consumers.cpp \
    -I"$CONDA_PREFIX/include" \
    -L"$CONDA_PREFIX/lib" \
    -ladios2_cxx \
    -Wl,-rpath,"$CONDA_PREFIX/lib" \
    -pthread \
    -o consumers