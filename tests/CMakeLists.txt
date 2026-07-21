# Testing suite for MarketPulse Intelligence Engine
# Zero-dependency bespoke runner for mathematical verification.

add_executable(mpie_math_tests
    main_test.cpp
    math/test_price_features.cpp
    math/test_book_features.cpp
    math/test_flow_features.cpp
    math/test_microstructure_features.cpp
    math/test_statistical_features.cpp
    math/test_edge_cases.cpp
    math/test_pipeline_integration.cpp
    storage/test_feature_store.cpp
    egress/test_shm_egress.cpp
)

target_include_directories(mpie_math_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_features(mpie_math_tests PRIVATE cxx_std_20)
target_link_libraries(mpie_math_tests PRIVATE md_mpie Threads::Threads)

add_test(NAME mpie_math_tests COMMAND mpie_math_tests)
