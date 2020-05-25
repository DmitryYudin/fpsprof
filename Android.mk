LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := fpsprof
LOCAL_SRC_FILES += $(sort $(wildcard $(LOCAL_PATH)/src/*.c*))
LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
LOCAL_CPPFLAGS += -Wall
LOCAL_EXPORT_C_INCLUDES += $(LOCAL_PATH)/include
include $(BUILD_STATIC_LIBRARY)

ifeq (1,$(strip $(BUILD_TEST_fpsprof)))
    fpsprofiler_SRC := $(LOCAL_PATH)/test/fpsprof.cc
    test_c_SRC := $(LOCAL_PATH)/test/test_c.c
    test_cpp_SRC := $(LOCAL_PATH)/test/test_cpp.cc
	define MACRO_TEST_APP # use $app variable
        include $$(CLEAR_VARS) 
        LOCAL_MODULE := $${app}
        LOCAL_SRC_FILES := ${${app}_SRC}
        LOCAL_CPPFLAGS += -Wall
        LOCAL_STATIC_LIBRARIES := fpsprof
        include $$(BUILD_EXECUTABLE)
    endef
    $(foreach app, fpsprofiler test_c test_cpp,$(eval $(call MACRO_TEST_APP)))
endif
