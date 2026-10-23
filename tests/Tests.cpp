// Copyright © 2026 CCP ehf.

#include "gtest/gtest.h"
#include "../include/pdm.h"
#include "../src/utilities.h"

TEST(DataCollection, GetEqualMetrics)
{
	auto metrics1 = PDM::RetrievePDMData("Tests", "1.0");
	auto metrics2 = PDM::RetrievePDMData("Tests", "1.0");
	EXPECT_EQ(metrics1.data, metrics2.data);
}

TEST(DataCollection, CanGetMetrics)
{
	auto data = PDM::RetrievePDMData("Tests", "1.0");
	EXPECT_FALSE(data.data.name.empty());
	EXPECT_FALSE(data.data.subitems.empty());
}

bool isValidUTF8(const std::string_view str)
{
	for (unsigned long long i = 0, len = str.length(); i < len; i++)
	{
		unsigned char c = str[i];
		int n = 0;
		if (0x00 <= c && c <= 0x7f) n=0;  // 0bbbbbbb
		else if ((c & 0xE0) == 0xC0) n=1; // 110bbbbb
		else if ( c ==0xED && i < (len-1) && (str[i+1] & 0xa0) == 0xa0) return false; //U+d800 to U+dfff
		else if ((c & 0xF0) == 0xE0) n=2; // 1110bbbb
		else if ((c & 0xF8) == 0xF0) n=3; // 11110bbb
		else return false;
		for (int j = 0; j < n && i < len; j++)
		{
			// n bytes matching 10bbbbbb follow ?
			if ((++i == len) || ((str[i] & 0xC0) != 0x80)) return false;
		}
	}
	return true;
}

bool isValidUTF8(const PDM::SubItem& item)
{
	for (auto subitem : item.items)
	{
		if (!isValidUTF8(subitem.name)) return false;
		if (!isValidUTF8(subitem.value)) return false;
	}
	for (auto subitem : item.subitems)
	{
		if (!isValidUTF8(subitem)) return false;
	}

	return true;
}

TEST(Unicode, DataIsValidUTF8)
{
	auto data = PDM::RetrievePDMData("Tests", "1.0");
	EXPECT_TRUE(isValidUTF8(data.data));
}

std::vector<std::pair<std::wstring, std::string>> validUnicodeStrings
{
	{L"The quick brown fox jumps over the lazy dog", u8"The quick brown fox jumps over the lazy dog"},
	{L"Wolther spillede på xylofon.", u8"Wolther spillede på xylofon."},
	{L"Zwölf Boxkämpfer jagten Eva quer über den Sylter Deich", u8"Zwölf Boxkämpfer jagten Eva quer über den Sylter Deich"},
	{L"Γαζέες καὶ μυρτιὲς δὲν θὰ βρῶ πιὰ στὸ χρυσαφὶ ξέφωτο", u8"Γαζέες καὶ μυρτιὲς δὲν θὰ βρῶ πιὰ στὸ χρυσαφὶ ξέφωτο"},
	{L"El pingüino Wenceslao hizo kilómetros bajo exhaustiva lluvia y frío, añoraba a su querido cachorro.", u8"El pingüino Wenceslao hizo kilómetros bajo exhaustiva lluvia y frío, añoraba a su querido cachorro."},
	{L"Le cœur déçu mais l'âme plutôt naïve, Louÿs rêva de crapaüter en canoë au delà des îles, près du mälström où brûlent les novæ.", u8"Le cœur déçu mais l'âme plutôt naïve, Louÿs rêva de crapaüter en canoë au delà des îles, près du mälström où brûlent les novæ."},
	{L"D'fhuascail Íosa, Úrmhac na hÓighe Beannaithe, pór Éava agus Ádhaimh", u8"D'fhuascail Íosa, Úrmhac na hÓighe Beannaithe, pór Éava agus Ádhaimh"},
	{L"Árvíztűrő tükörfúrógép", u8"Árvíztűrő tükörfúrógép"},
	{L"Sævör grét áðan því úlpan var ónýt", u8"Sævör grét áðan því úlpan var ónýt"},
	{L"いろはにほへとちりぬる イロハニホヘト", u8"いろはにほへとちりぬる イロハニホヘト"},
	{L"Pchnąć w tę łódź jeża lub ośm skrzyń fig", u8"Pchnąć w tę łódź jeża lub ośm skrzyń fig"},
	{L"В чащах юга жил бы цитрус? Да, но фальшивый экземпляр!", u8"В чащах юга жил бы цитрус? Да, но фальшивый экземпляр!"},
	{L"๏ เป็นมนุษย์สุดประเสริฐเลิศคุณค่า  กว่าบรรดาฝูงสัตว์เดรัจฉาน", u8"๏ เป็นมนุษย์สุดประเสริฐเลิศคุณค่า  กว่าบรรดาฝูงสัตว์เดรัจฉาน"},
	{L"Pijamalı hasta, yağız şoföre çabucak güvendi.", u8"Pijamalı hasta, yağız şoföre çabucak güvendi."},
	{L"∮ E⋅da = Q,  n → ∞, ∑ f(i) = ∏ g(i),      ⎧⎡⎛┌─────┐⎞⎤⎫", u8"∮ E⋅da = Q,  n → ∞, ∑ f(i) = ∏ g(i),      ⎧⎡⎛┌─────┐⎞⎤⎫"},
	{L"ði ıntəˈnæʃənəl fəˈnɛtık əsoʊsiˈeıʃn", u8"ði ıntəˈnæʃənəl fəˈnɛtık əsoʊsiˈeıʃn"},
	{L"((V⍳V)=⍳⍴V)/V←,V    ⌷←⍳→⍴∆∇⊃‾⍎⍕⌈", u8"((V⍳V)=⍳⍴V)/V←,V    ⌷←⍳→⍴∆∇⊃‾⍎⍕⌈"},
	{L"STARGΛ̊TE SG-1, a = v̇ = r̈, a⃑ ⊥ b⃑", u8"STARGΛ̊TE SG-1, a = v̇ = r̈, a⃑ ⊥ b⃑"},
	{L"გთხოვთ ახლავე გაიაროთ რეგისტრაცია Unicode-ის მეათე საერთაშორისო", u8"გთხოვთ ახლავე გაიაროთ რეგისტრაცია Unicode-ის მეათე საერთაშორისო"},
	{L"ሰማይ አይታረስ ንጉሥ አይከሰስ።", u8"ሰማይ አይታረስ ንጉሥ አይከሰስ።"},
	{L"ᚻᛖ ᚳᚹᚫᚦ ᚦᚫᛏ ᚻᛖ ᛒᚢᛞᛖ ᚩᚾ ᚦᚫᛗ ᛚᚪᚾᛞᛖ ᚾᚩᚱᚦᚹᛖᚪᚱᛞᚢᛗ ᚹᛁᚦ ᚦᚪ ᚹᛖᛥᚫ", u8"ᚻᛖ ᚳᚹᚫᚦ ᚦᚫᛏ ᚻᛖ ᛒᚢᛞᛖ ᚩᚾ ᚦᚫᛗ ᛚᚪᚾᛞᛖ ᚾᚩᚱᚦᚹᛖᚪᚱᛞᚢᛗ ᚹᛁᚦ ᚦᚪ ᚹᛖᛥᚫ"},
	{L"⡌⠁⠧⠑ ⠼⠁⠒  ⡍⠜⠇⠑⠹⠰⠎ ⡣⠕⠌", u8"⡌⠁⠧⠑ ⠼⠁⠒  ⡍⠜⠇⠑⠹⠰⠎ ⡣⠕⠌"},
	{L"∀∂∈ℝ∧∪≡∞ ↑↗↨↻⇣ ┐┼╔╘░►☺♀ ﬁ�⑀₂ἠḂӥẄɐː⍎אԱა", u8"∀∂∈ℝ∧∪≡∞ ↑↗↨↻⇣ ┐┼╔╘░►☺♀ ﬁ�⑀₂ἠḂӥẄɐː⍎אԱა"},
	{L"╠╡ ╳ ╞╣  ├╢   ╟┤  ├┼─┼─┼┤  ├╫─╂─╫┤  ┣┿╾┼╼┿┫  ┕┛┖┚     ┌┄┄┐ ╎ ┏┅┅┓ ┋ ▍ ╲╱╲╱╳╳╳", u8"╠╡ ╳ ╞╣  ├╢   ╟┤  ├┼─┼─┼┤  ├╫─╂─╫┤  ┣┿╾┼╼┿┫  ┕┛┖┚     ┌┄┄┐ ╎ ┏┅┅┓ ┋ ▍ ╲╱╲╱╳╳╳"},
	{L"爱吃鱼的在我家", u8"爱吃鱼的在我家"},
	{L"🐱", u8"🐱"},// Surrogate pair in UTF-16
};

TEST(Unicode, CanConvertUTF8ToWStrings)
{
	for (auto [wstr, utf8] : validUnicodeStrings)
		EXPECT_EQ(PDM::UTF8ToWString(utf8), wstr);
}

#if _WIN32

TEST(Unicode, CanConvertWStringsToUTF8)
{
	for (auto [wstr, utf8] : validUnicodeStrings)
		EXPECT_EQ(PDM::WStringToUTF8(wstr), utf8);
}

TEST(Unicode, ConvertingStringsTwiceLeavesThemUnchanged)
{
	for (auto [wstr, utf8] : validUnicodeStrings)
	{
		EXPECT_EQ(PDM::WStringToUTF8(PDM::UTF8ToWString(utf8)), utf8);
		EXPECT_EQ(PDM::UTF8ToWString(PDM::WStringToUTF8(wstr)), wstr);
	}
}

TEST(Unicode, CanConvertUTF8AndWStringsToNative)
{
	for (auto [wstr, utf8] : validUnicodeStrings)
		EXPECT_EQ(PDM::UTF8ToNative(utf8), PDM::WStringToNative(wstr));
}

#endif