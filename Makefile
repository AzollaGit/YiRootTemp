#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := $(notdir $(shell pwd))

include $(IDF_PATH)/project.mk
