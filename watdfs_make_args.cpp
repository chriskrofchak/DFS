#include "watdfs_make_args.h"
#include "rpc.h"
#include "sys/types.h"
#include "string.h"

///// ENCODING FUNCTIONS
int encode_arg_path(const char* path) {
    return (1u << ARG_INPUT) | (1u << ARG_ARRAY) | (ARG_CHAR << 16u) | (uint)(strlen(path)+1);
}

int encode_retcode() {
    return (1u << ARG_OUTPUT) | (ARG_INT << 16u);
}

int encode_arg_type(bool is_input, bool is_output, bool is_array, int type, int arr_size) {
    return ((uint)is_input << ARG_INPUT)   | 
           ((uint)is_output << ARG_OUTPUT) |
           ((uint)is_array << ARG_ARRAY)   |
           (uint)(type << 16u)             | 
           (uint)arr_size;
}