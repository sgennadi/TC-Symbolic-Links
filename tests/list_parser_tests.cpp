#include "ListParser.h"
#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<std::byte> utf16(const std::u16string& value) {
    std::vector<std::byte> bytes{std::byte{0xFF}, std::byte{0xFE}};
    for (const char16_t codeUnit : value) {
        bytes.push_back(std::byte(codeUnit & 0xFF));
        bytes.push_back(std::byte((codeUnit >> 8) & 0xFF));
    }
    return bytes;
}

void expectEqual(const std::u16string& input, std::vector<std::u16string> expected) {
    if (tcs::ParseTcWideList(utf16(input)) != expected)
        throw std::runtime_error("Unexpected parsed file list");
}

void expectReject(const std::vector<std::byte>& bytes) {
    try {
        (void)tcs::ParseTcWideList(bytes);
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("Invalid UTF-16 input was accepted");
}
}

int main() {
    // ASCII, Cyrillic and whitespace.
    expectEqual(u"C:\\Temp\\a.txt\r\nC:\\Папка\\файл.txt\r\nC:\\A B\\x\r\n",
        {u"C:\\Temp\\a.txt", u"C:\\Папка\\файл.txt", u"C:\\A B\\x"});
    expectEqual(u"\"C:\\Space dir\\test.txt\"\n", {u"C:\\Space dir\\test.txt"});

    // Hebrew RTL folders, mixed Hebrew/English/numbers and Hebrew punctuation.
    expectEqual(u"C:\\משתמשים\\דוד\\מסמכים\\קובץ.txt\r\n",
        {u"C:\\משתמשים\\דוד\\מסמכים\\קובץ.txt"});
    expectEqual(u"D:\\Projects\\פרויקט 2026\\Report דו\u05F4ח final 42.txt\r\n"
                u"C:\\Users\\משתמש\\Desktop\\תיקייה עם רווחים\\נתונים.csv\r\n",
        {u"D:\\Projects\\פרויקט 2026\\Report דו\u05F4ח final 42.txt",
         u"C:\\Users\\משתמש\\Desktop\\תיקייה עם רווחים\\נתונים.csv"});

    // Hebrew niqqud: preserve combining code points without NFC/NFD conversion.
    expectEqual(u"C:\\עברית\\ש\u05B8\u05C1לו\u05B9ם.txt\r\n",
        {u"C:\\עברית\\ש\u05B8\u05C1לו\u05B9ם.txt"});

    // Quoted paths with no final newline and non-BMP surrogate pairs.
    expectEqual(u"\"C:\\מסמכים חדשים\\תמונה \U0001F4C1.txt\"",
        {u"C:\\מסמכים חדשים\\תמונה \U0001F4C1.txt"});
    expectEqual(u"\"C:\\תיקייה בעברית\\קובץ עם רווח.txt\"\r\n",
        {u"C:\\תיקייה בעברית\\קובץ עם רווח.txt"});
    expectEqual(u"C:\\תיקייה\u05D0.txt", {u"C:\\תיקייה\u05D0.txt"});

    // Known UTF-16LE bytes for aleph (U+05D0) followed by CRLF.
    {
        const std::vector<std::byte> bytes{std::byte{0xFF}, std::byte{0xFE},
            std::byte{0xD0}, std::byte{0x05}, std::byte{0x0D}, std::byte{0x00},
            std::byte{0x0A}, std::byte{0x00}};
        if (tcs::ParseTcWideList(bytes) != std::vector<std::u16string>{u"\u05D0"})
            throw std::runtime_error("Hebrew UTF-16LE decoding failed");
    }

    // Reject bad BOM, odd byte count, NUL and invalid surrogate code units.
    expectReject({std::byte{0xD0}, std::byte{0x05}});
    expectReject({std::byte{0xFF}, std::byte{0xFE}, std::byte{0xD0}});
    expectReject(utf16({u'C', u':', u'\\', static_cast<char16_t>(0xD800)}));
    expectReject(utf16({u'C', u':', u'\\', static_cast<char16_t>(0xDC00)}));
    expectReject(utf16({u'C', u':', u'\\', static_cast<char16_t>(0xD800), u'\r', u'\n'}));
    expectReject(utf16({u'C', u':', u'\\', u'a', u'\0', u'b'}));
    expectEqual(u"", {});  // BOM only.
    return 0;
}
