cmake_minimum_required(VERSION 3.0.0)
project(sds VERSION 0.1.0)
add_library(sds sds.c)
set_property(TARGET sds PROPERTY C_STANDARD 99)