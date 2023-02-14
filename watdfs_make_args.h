#ifndef MAKE_ARGS_H
#define MAKE_ARGS_H

#define MAKE_ARGS(__ARG_COUNT, __PATH)              \
    void **args = new void*[__ARG_COUNT];           \
    args[0] = (void *)__PATH;                       \
    int retcode = 0;                                \
    args[__ARG_COUNT-1] = (void*)&retcode          

#define MAKE_ARG_TYPES(__ARG_COUNT) int arg_types[__ARG_COUNT+1]

#define SETUP_ARG_TYPES(__ARG_COUNT, __PATH)        \
    arg_types[0] = encode_arg_path(__PATH);         \
    arg_types[__ARG_COUNT-1] = encode_retcode();    \
    arg_types[__ARG_COUNT] = 0

// encoding parameters
int encode_arg_path(const char* path);
int encode_retcode();
int encode_arg_type(bool is_input, bool is_output, bool is_array, int type, int arr_size);

// to not repeat work in client and server
// void mknod_args(void **args);
// void open_args(void **args);
// void mknod_arg_types(int *arg_types);
// void open_arg_types(int *arg_types);




#endif