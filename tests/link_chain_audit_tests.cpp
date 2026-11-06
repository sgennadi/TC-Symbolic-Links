#include "LinkChainWalk.h"
#include "LinkAuditCsv.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>

int main() {
    using tcs::chain::Kind;
    using tcs::chain::Status;
    using tcs::chain::Observation;
    std::map<std::wstring, Observation> nodes;
    auto observe = [&](const std::wstring& name) -> Observation {
        auto it = nodes.find(name);
        return it == nodes.end() ? Observation{Kind::Regular,L"",false,2} : it->second;
    };
    auto resolve = [](const std::wstring&, const Observation& value) {
        return value.target;
    };
    assert(tcs::chain::Walk(L"", observe, resolve).status == Status::Error);
    nodes[L"C:\\file.txt"] = {Kind::Regular,L"",false,0};
    auto report = tcs::chain::Walk(L"C:\\file.txt", observe, resolve);
    assert(report.status == Status::NotALink && report.hops.empty());
    nodes[L"C:\\link-A"] = {Kind::FileSymlink,L"C:\\link-B",false,0};
    nodes[L"C:\\link-B"] = {Kind::DirectorySymlink,L"C:\\file.txt",true,0};
    report = tcs::chain::Walk(L"C:\\link-A", observe, resolve);
    assert(report.status == Status::Resolved && report.hops.size() == 2);
    assert(report.hops[1].relative);
    assert(report.terminalPath == L"C:\\file.txt");

    nodes[L"C:\\link-B"] = {Kind::FileSymlink,L"C:\\link-A",false,0};
    report = tcs::chain::Walk(L"C:\\link-A", observe, resolve);
    assert(report.status == Status::Loop && report.hops.size() == 2);
    assert(report.terminalPath == L"C:\\link-A");

    nodes[L"C:\\link-B"] = {Kind::FileSymlink,L"C:\\missing",false,0};
    report = tcs::chain::Walk(L"C:\\link-A", observe, resolve);
    assert(report.status == Status::Broken && report.error == 2);
    nodes[L"C:\\missing"] = {Kind::Regular,L"",false,5};
    report = tcs::chain::Walk(L"C:\\link-A", observe, resolve);
    assert(report.status == Status::AccessDenied && report.error == 5);
    nodes[L"C:\\missing"] = {Kind::Unsupported,L"",false,0};
    report = tcs::chain::Walk(L"C:\\link-A", observe, resolve);
    assert(report.status == Status::Unsupported);

    nodes[L"C:\\link-B"] = {Kind::FileSymlink,L"C:\\file.txt",false,0};
    report = tcs::chain::Walk(L"C:\\link-A", observe, resolve, 1);
    assert(report.status == Status::HopLimit && report.hops.size() == 1);
    assert(report.terminalPath == L"C:\\link-B");
    report = tcs::chain::Walk(L"C:\\link-A", observe, resolve,
                              400 /* must be capped */);
    assert(report.status == Status::Resolved);
    nodes[L"C:\\link-B"] = {Kind::FileSymlink,L"",false,0};
    report = tcs::chain::Walk(L"C:\\link-B", observe, resolve);
    assert(report.status == Status::Error);
    assert(tcs::chain::Key(L"C:/ABC/") == tcs::chain::Key(L"c:\\abc"));

    using tcs::audit::Cell;
    assert(Cell(L"plain") == L"\"plain\"");
    assert(Cell(L"a\"b") == L"\"a\"\"b\"");
    assert(Cell(L"=2+2") == L"\"'=2+2\"");
    assert(Cell(L"  @cmd") == L"\"  '@cmd\"");
    assert(Cell(L"+SUM(1)") == L"\"'+SUM(1)\"");
    assert(Cell(L"-42") == L"\"'-42\"");
    assert(Cell(L"\tfoo\r\nbar") == L"\" foo  bar\"");
    const std::vector<tcs::audit::Record> rows = {{
        L"C:\\קישור\\ссылка.txt",L"File symlink",L"Valid",
        L"\\\\fileserver\\data\\file,one.txt",L"C:\\other.txt",
        false,0,0
    }};
    const std::wstring csv = tcs::audit::Build(rows);
    assert(csv.find(L"Path,Type,Health,Target") == 0);
    assert(csv.find(L"\\\\fileserver\\data\\file,one.txt") != std::wstring::npos);
    assert(csv.find(L"קישור") != std::wstring::npos);
    assert(csv.find(L"ссылка") != std::wstring::npos);
    assert(csv.find(L"\r\n") != std::wstring::npos);
}
