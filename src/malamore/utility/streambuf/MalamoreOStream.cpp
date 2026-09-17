//
// Created by mbda on 16/09/2026.
//

//
// Created by Giacomo Bergami on 16/09/2026.
//

#include "malamore/utility/streambuf/MalamoreOStream.h"

MalamoreOStream::MalamoreOStream()  {

}

MalamoreOStream::~MalamoreOStream() {
    original.Close();
}

NTSTATUS MalamoreOStream::Open(const MalamorePath& path, ULONG flags) {
    return original.Open(path, flags);
}

VOID MalamoreOStream::Close() {
    original.Close();
}

MalamoreOStream& MalamoreOStream::operator<<(const char* str) {
    original << str; return *this;
}

MalamoreOStream& MalamoreOStream::operator<<(char c) {
    original << c; return *this;
}

MalamoreOStream& MalamoreOStream::operator<<(const WCHAR* wstr) {
    original << wstr;
    return *this;
}

MalamoreOStream& MalamoreOStream::operator<<(ULONG value) {
    original << value;
    return *this;
}

MalamoreOStream& MalamoreOStream::operator<<(ULONG64 value) {
    original << value;
    return *this;
}

MalamoreOStream& MalamoreOStream::operator<<(MalamoreOStream& (*manipulator)(MalamoreOStream&)) {
    return manipulator(*this);
}
