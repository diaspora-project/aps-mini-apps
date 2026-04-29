find_package(Python3 REQUIRED COMPONENTS Interpreter Development NumPy)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/ModDistStreamPubDemo.py
          ${CMAKE_CURRENT_SOURCE_DIR}/diaspora_dist.py
     DESTINATION ${CMAKE_BINARY_DIR}/python/streamer-dist/)
