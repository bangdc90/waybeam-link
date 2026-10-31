extern "C" unsigned long int legacy_strtoul(
    const char* value, char** end, int base) noexcept __asm__("strtoul");

extern "C" unsigned long int __isoc23_strtoul(
    const char* value, char** end, int base) noexcept
{
    return legacy_strtoul(value, end, base);
}