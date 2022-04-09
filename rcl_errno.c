#include "rcl_errno.h"

#include <string.h>

__thread int rcl_errno;

const char *rcl_strerror(int ec) {
    switch (ec) {
        case RCL_EARGS:
            return "invalid args";
    }
    return strerror(ec);
}
