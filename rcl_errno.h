#ifndef ERR_H
#define ERR_H

extern __thread int rcl_errno;

enum rcl_errno {
    RCL_EARGS = 200,
};

const char *rcl_strerror(int ec);

static inline const char *rcl_errno_str() {
    return  rcl_strerror(rcl_errno);
}

static inline void rcl_errno_set(int ec) {
    rcl_errno = ec;
}

#endif // !ERR_H
