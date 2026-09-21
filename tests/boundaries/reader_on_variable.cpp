#include "Serializer.hpp"

std::vector<std::uint8_t> make(void)
{
    return {1, 2, 3};
}

int main()
{
    const auto bytes = make();
    kuge::ByteReader reader(bytes);
    return static_cast<int>(reader.remaining()) - 3;
}
