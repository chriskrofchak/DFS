#include "openbook.h"

fd_pair OpenBook::get_fd_pair(std::string filename) {
    return open_files.at(filename); 
}

int OpenBook::get_local_fd(std::string filename) {
    if (!local_fd.count( open_files.at(filename).cli_fd )) return -EBADF; // bad file descriptor
    // else
    return local_fd.at( open_files.at(filename).cli_fd ); 
}

int OpenBook::get_local_fd(int fd) {
    if (!local_fd.count(fd)) return -EBADF; // bad file descriptor
    // else
    return local_fd.at(fd); 
}

int OpenBook::get_server_fd(std::string filename) {
    return server_fd.at(get_fd_pair(filename).cli_fd); 
}

void OpenBook::set_cli_fd(std::string filename, int fd) {
    local_fd.at(get_fd_pair(filename).cli_fd) = fd;
}

std::string OpenBook::get_cache() { 
    return cache_path_;
}

time_t OpenBook::get_interval() { 
    return t;
}

time_t OpenBook::get_last_validated(std::string filename) { 
    return Tc.at(filename);
}

void OpenBook::set_validate(std::string filename, time_t new_Tc) { 
    if (!Tc.count(filename)) {

        Tc.insert(std::pair<std::string, time_t>(filename, new_Tc));

    } else {

        Tc.at(filename) = new_Tc; // update Tc

    }

}

bool OpenBook::is_open(const char *path) { 
    return open_files.count(std::string(path)); 
}

int OpenBook::OB_open(
    std::string filename, 
    int cli_fd, 
    uint64_t cli_flags, 
    int ser_fd, 
    uint64_t ser_flags) 
{
    if (open_files.count(filename)) return -EMFILE; // BAD
    // else 
    fd_pair fdp(cli_fd, ser_fd, cli_flags, ser_flags);
    open_files.insert(std::pair<std::string, fd_pair>(filename, fdp));
    local_fd.insert({ cli_fd, cli_fd });
    server_fd.insert({ cli_fd, ser_fd});
    return 0;
}

int OpenBook::OB_close(std::string filename) {
    int fd = open_files.at(filename).cli_fd;
    open_files.erase(filename);
    local_fd.erase(fd);
    server_fd.erase(fd);
    return 0;
}