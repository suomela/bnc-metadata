#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <pugixml.hpp>

using namespace std::string_literals;
using namespace boost::filesystem;
using namespace pugi;

struct Record {
    void tell(std::string attr, std::string value) {
        boost::trim(value);
        if (attr == "xml:id") {
            id = value;
        } else {
            assert(param.count(attr) == 0);
            param[attr] = value;
        }
    }

    void dump() {
        std::cout << id << ":";
        for (auto p: param) {
            std::cout << " " << p.first << "=" << p.second;
        }
        std::cout << "\n";
    }

    std::string id;
    std::map<std::string, std::string> param;
};

class File {
public:
    File(std::string stem_) : stem{stem_}
    {}

    void parse(const xml_document& doc) {
        xml_node root = doc.child("bncDoc");
        assert(root);
        xml_node stext = root.child("stext");
        if (!stext) {
            return;
        }
        std::string id = root.attribute("xml:id").value();
        assert(id == stem);
        std::cout << stem << "\n\n";
        xml_node head = root.child("teiHeader");
        assert(head);
        xml_node prof = head.child("profileDesc");
        assert(prof);
        parse_people(prof.child("particDesc"));
        parse_settings(prof.child("settingDesc"));
    }

private:
    void parse_people(xml_node parent) {
        for (xml_node node: parent.children("person")) {
            Record p;
            for (xml_attribute attr: node.attributes()) {
                p.tell(attr.name(), attr.value());
            }
            for (xml_node child: node.children()) {
                std::string name = child.name();
                if (name == "dialect") {
                    name = "dialectDetail";
                }
                p.tell(name, child.child_value());
            }
            std::cout << stem << ": person ";
            p.dump();
        }
        std::cout << "\n";
    }

    void parse_settings(xml_node parent) {
        for (xml_node node: parent.children("setting")) {
            Record p;
            for (xml_attribute attr: node.attributes()) {
                p.tell(attr.name(), attr.value());
            }
            for (xml_node child: node.children()) {
                std::string name = child.name();
                p.tell(name, child.child_value());
            }
            std::cout << stem << ": setting ";
            p.dump();
        }
        std::cout << "\n";
    }

    const std::string stem;
};

bool process(const path& p) {
    std::string stem = p.stem().string();
    xml_document doc;
    xml_parse_result result = doc.load_file(p.c_str());
    if (!result) {
        std::cerr << stem << ": error: " << result.description() << std::endl;
        return false;
    }
    File file(stem);
    file.parse(doc);
    return true;
}

bool process_all(const path& p) {
    if (p.has_extension() && p.extension().string() == ".xml") {
        return process(p);
    } else if (is_directory(p)) {
        bool ok = true;
        std::vector<path> dir;
        for (auto& f : directory_iterator(p)) {
            dir.push_back(f.path());
        }
        std::sort(dir.begin(), dir.end());
        for (auto& f : dir) {
            if (!process_all(f)) {
                ok = false;
            }
        }
        return ok;
    } else {
        return true;
    }
}

int main(int argc, const char** argv) {
    if (argc == 1) {
        std::cout << "usage: bnc-metadata BNC-DIRECTORY ..." << std::endl;
        exit(1);
    }
    try {
        for (int i = 1; i < argc; ++i) {
            process_all(argv[i]);
        }
    }
    catch (filesystem_error e) {
        std::cerr << e.what() << std::endl;
        exit(1);
    }
}
