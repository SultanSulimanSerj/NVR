#include <gtest/gtest.h>

#include <string>

TEST(OnvifXml, FindTagSimple) {
    auto xml = R"(<Foo><tt:Manufacturer>Acme</tt:Manufacturer></Foo>)";

    auto find = [](const std::string& s, const std::string& tag) {
        auto open  = "<" + tag + ">";
        auto close = "</" + tag + ">";
        auto a = s.find(open);
        if (a == std::string::npos) return std::string{};
        a += open.size();
        auto b = s.find(close, a);
        return (b == std::string::npos) ? std::string{} : s.substr(a, b - a);
    };
    EXPECT_EQ(find(xml, "tt:Manufacturer"), "Acme");
}
