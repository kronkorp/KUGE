// Must NOT compile: a ByteReader on a temporary vector would read freed memory
#include "Serializer.hpp"

std::vector<std::uint8_t> make(void)
{
    return {1, 2, 3};
}

int main()
{
    kuge::ByteReader reader(make());
    return static_cast<int>(reader.remaining());
}
