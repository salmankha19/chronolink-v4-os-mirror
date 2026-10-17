idf_component_register(
    SRCS
        "src/svc_prayer_engine.cpp"
        "src/prayertimes/PrayerTimes.cpp"
    INCLUDE_DIRS "include"
    REQUIRES
        log
        esp_libc
)

# Embedded C++ optimization: disable exceptions and RTTI to save flash/RAM.
target_compile_options(${COMPONENT_LIB} PRIVATE -fno-exceptions -fno-rtti)
