#pragma once

#include <stdexcept>

#include <dlfcn.h>


class DynamicLibraryHolder {
public:
    DynamicLibraryHolder(const char* filename, int flags=RTLD_NOW|RTLD_DEEPBIND) {
        void* lib = dlopen(filename, flags);
        if (!lib) {
            throw std::runtime_error("failed to open dynamic library");
        } else {
            Library_ = lib;
        }
    }
    
    ~DynamicLibraryHolder() {
        if (Library_) {
            dlclose(Library_);
        }
    }

    DynamicLibraryHolder(DynamicLibraryHolder&& rhs) {
        std::swap(Library_, rhs.Library_);
    }

    DynamicLibraryHolder& operator=(DynamicLibraryHolder&& rhs) {
        std::swap(Library_, rhs.Library_);
        return *this;
    }

    DynamicLibraryHolder(const DynamicLibraryHolder& rhs) = delete;
    DynamicLibraryHolder& operator=(const DynamicLibraryHolder& rhs) = delete;

    void* Symbol(const char* symbol) {
        if (!Library_) {
            throw std::runtime_error("library not initialized");
        }
        return dlsym(Library_, symbol);
    }

    static void* CheckedNextSymbol(const char* symbol) {
        void* retval = dlsym(RTLD_NEXT, symbol);
        if (!retval) {
            throw std::runtime_error("failed to find next symbol");
        }
        return retval;
    }

    static DynamicLibraryHolder ThisBinary() {
        return DynamicLibraryHolder(nullptr, RTLD_LAZY | RTLD_NOW);
    }


private:
    void* Library_{ nullptr };
};
