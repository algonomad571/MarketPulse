add_executable(throughput_benchmark
    throughput_benchmark.cpp
)

target_link_libraries(throughput_benchmark
    PRIVATE
        md_core
)
