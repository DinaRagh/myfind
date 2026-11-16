
#include <iostream>
#include <filesystem>
#include <unistd.h>
#include <sys/wait.h>
#include <getopt.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <fcntl.h>
#include <cerrno>
#include <cstdlib>
#include <cstdio>

namespace fs = std::filesystem;


bool writeAll(int fd, const std::string& output) {

    size_t written = 0;

    while (written < output.size()) {

        ssize_t result = write(
            fd,
            output.data() + written,
            output.size() - written
        );

        if (result < 0) {

            if (errno == EINTR) {
                continue;
            }

            return false;
        }

        if (result == 0) {
            return false;
        }

        written += static_cast<size_t>(result);
    }

    return true;
}


void printResult(int lockFd,
                 const std::string& output) {

    struct flock lock = {};

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;

    while (fcntl(lockFd, F_SETLKW, &lock) == -1) {

        if (errno == EINTR) {
            continue;
        }

        perror("fcntl lock");
        return;
    }

    if (!writeAll(STDOUT_FILENO, output)) {
        perror("write");
    }

    lock.l_type = F_UNLCK;

    if (fcntl(lockFd, F_SETLK, &lock) == -1) {
        perror("fcntl unlock");
    }
}


bool compareNames(const std::string& a,
                  const std::string& b,
                  bool insensitive) {

    if (!insensitive) {
        return a == b;
    }

    std::string lowerA = a;
    std::string lowerB = b;

    std::transform(
        lowerA.begin(),
        lowerA.end(),
        lowerA.begin(),
        [](unsigned char c) {
            return std::tolower(c);
        }
    );

    std::transform(
        lowerB.begin(),
        lowerB.end(),
        lowerB.begin(),
        [](unsigned char c) {
            return std::tolower(c);
        }
    );

    return lowerA == lowerB;
}


void searchFile(const std::string& searchpath,
                const std::string& filename,
                bool recursive,
                bool insensitive,
                int lockFd) {

    auto checkEntry = [&](const fs::directory_entry& entry) {

        std::error_code ec;

        if (!entry.is_regular_file(ec) || ec) {
            return;
        }

        std::string currentName =
            entry.path().filename().string();

        if (compareNames(currentName, filename, insensitive)) {

            fs::path absolutePath =
                fs::absolute(entry.path(), ec);

            if (ec) {
                std::cerr << "Error getting absolute path: "
                          << ec.message() << std::endl;
                return;
            }

            std::string output =
                std::to_string(getpid()) + ": " +
                filename + ": " +
                absolutePath.lexically_normal().string() +
                "\n";

            printResult(lockFd, output);
        }
    };


    std::error_code ec;

    // Rekursive Suche in allen Unterverzeichnissen.
    if (recursive) {

        fs::recursive_directory_iterator it(
            searchpath,
            fs::directory_options::skip_permission_denied,
            ec
        );

        fs::recursive_directory_iterator end;

        if (ec) {
            std::cerr << "Directory error: "
                      << ec.message() << std::endl;
            return;
        }

        while (it != end) {

            checkEntry(*it);

            it.increment(ec);

            if (ec) {
                std::cerr << "Traversal error: "
                          << ec.message() << std::endl;
                return;
            }
        }

    } else {

        // Suche nur im angegebenen Verzeichnis.
        fs::directory_iterator it(
            searchpath,
            fs::directory_options::skip_permission_denied,
            ec
        );

        fs::directory_iterator end;

        if (ec) {
            std::cerr << "Directory error: "
                      << ec.message() << std::endl;
            return;
        }

        while (it != end) {

            checkEntry(*it);

            it.increment(ec);

            if (ec) {
                std::cerr << "Traversal error: "
                          << ec.message() << std::endl;
                return;
            }
        }
    }
}


int main(int argc, char* argv[]) {

    bool recursive = false;
    bool insensitive = false;

    int opt;

    while ((opt = getopt(argc, argv, "Ri")) != -1) {

        switch (opt) {

            case 'R':
                recursive = true;
                break;

            case 'i':
                insensitive = true;
                break;

            default:
                std::cerr
                    << "Usage: ./myfind [-R] [-i] "
                    << "searchpath filename1 [filename2 ...]"
                    << std::endl;

                return 1;
        }
    }


    if (argc - optind < 2) {

        std::cerr
            << "Error: Searchpath and at least one filename required!"
            << std::endl;

        return 1;
    }


    std::string searchpath = argv[optind];


    std::vector<std::string> filenames;

    for (int i = optind + 1; i < argc; i++) {

        filenames.push_back(argv[i]);
    }


    std::error_code ec;

    if (!fs::is_directory(searchpath, ec) || ec) {

        std::cerr << "Error: Invalid search directory: "
                  << searchpath << std::endl;

        return 1;
    }


    std::cout << "Searchpath: " << searchpath << std::endl;

    std::cout << "Recursive: "
              << (recursive ? "yes" : "no") << std::endl;

    std::cout << "Case-insensitive: "
              << (insensitive ? "yes" : "no") << std::endl;

    std::cout << "Files to search:" << std::endl;

    for (const std::string& filename : filenames) {

        std::cout << "- " << filename << std::endl;
    }

    std::cout.flush();

    char lockTemplate[] = "/tmp/myfind-lock-XXXXXX";

    int lockFd = mkstemp(lockTemplate);

    if (lockFd == -1) {

        perror("mkstemp");
        return 1;
    }

    if (unlink(lockTemplate) == -1) {

        perror("unlink");
        close(lockFd);
        return 1;
    }


    int childCount = 0;

    for (const std::string& filename : filenames) {

        pid_t pid = fork();

        if (pid < 0) {

            perror("fork");

            continue;
        }

        else if (pid == 0) {

            searchFile(
                searchpath,
                filename,
                recursive,
                insensitive,
                lockFd
            );
            _exit(0);
        }

        else {
            childCount++;
        }
    }


    for (int i = 0; i < childCount; i++) {

        int status;

        pid_t result;

        do {

            result = waitpid(-1, &status, 0);

        } while (result == -1 && errno == EINTR);

        if (result == -1) {

            perror("waitpid");
            break;
        }

        if (WIFEXITED(status)) {

            if (WEXITSTATUS(status) != 0) {

                std::cerr << "Child process "
                          << result
                          << " exited with an error."
                          << std::endl;
            }
        }
    }


    close(lockFd);

    return 0;
}
