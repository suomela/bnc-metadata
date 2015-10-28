#include <cassert>
#include <iostream>
#include <map>
#include <set>
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
        } else if (attr == "n") {
            n = value;
        } else {
            assert(param.count(attr) == 0);
            param[attr] = value;
        }
    }

    void dump() {
        std::cout << id << ":";
        for (auto p : param) {
            std::cout << " " << p.first << "=" << p.second;
        }
        std::cout << "\n";
    }

    std::string id;
    std::string n;
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
        std::string type = stext.attribute("type").value();
        if (type == "OTHERSP") {
            return;
        }
        assert(type == "CONVRSN");
        std::string id = root.attribute("xml:id").value();
        assert(id == stem);
        std::cout << stem << "\n";
        xml_node head = root.child("teiHeader");
        xml_node source = head.child("fileDesc").child("sourceDesc");
        xml_node prof = head.child("profileDesc");
        parse_head(recordings, "recording", source.child("recordingStmt"));
        parse_head(people, "person", prof.child("particDesc"));
        parse_head(settings, "setting", prof.child("settingDesc"));
        parse_stext(stext);
    }

private:
    void parse_stext(xml_node stext) {
        for (xml_node rec : stext.children()) {
            assert(rec.name() == "div"s);
            parse_rec(rec);
        }
    }

    void parse_rec(xml_node rec) {
        std::string decls = rec.attribute("decls").value();
        std::string n = rec.attribute("n").value();
        std::string setting;
        if (decls.size() > 0) {
            std::vector<std::string> parts;
            boost::split(parts, decls, boost::is_space());
            assert(parts.size() == 2);
            assert(recordings.count(parts[0]) == 1);
            assert(settings.count(parts[1]) == 1);
            setting = parts[1];
        } else {
            assert(n.size() > 0);
            if (settings.count(n) == 1) {
                setting = n;
            } else {
                std::cerr << stem << ": " << n << ": unknown setting" << std::endl;
            }
        }

        for (xml_node u : rec.children()) {
            assert(u.name() == "u"s);
            parse_u(setting, u);
        }
    }

    void parse_u(std::string setting, xml_node u) {
        std::string who = u.attribute("who").value();
        assert(who.size() > 0);
        std::string person;
        if (people.count(who) == 1) {
            person = who;
        } else if (unknown_people.count(who) == 0) {
            unknown_people.insert(who);
            std::cerr << stem << ": " << who << ": unknown person" << std::endl;
        }

        for (xml_node s : u.children("s")) {
            parse_s(setting, person, s);
        }
    }

    void parse_s(std::string setting, std::string person, xml_node s) {
        std::string n = s.attribute("n").value();
        std::cout << stem << " " << n << " " << setting << " " << person << "\n";
    }

    void parse_head(std::map<std::string, Record>& target, std::string label, xml_node parent) const {
        assert(parent);
        for (xml_node node : parent.children(label.c_str())) {
            Record p;
            for (xml_attribute attr : node.attributes()) {
                p.tell(attr.name(), attr.value());
            }
            for (xml_node child : node.children()) {
                std::string name = child.name();
                if (name == "dialect"s) {
                    name = "dialectDetail"s;
                }
                p.tell(name, child.child_value());
            }
            // std::cout << stem << ": " << label << ": ";
            // p.dump();
            assert(p.id.size());
            assert(target.count(p.id) == 0);
            target[p.id] = p;
            if (p.n.size()) {
                assert(target.count(p.n) == 0);
                target[p.n] = p;
            }
        }
        // std::cout << "\n";
    }

    const std::string stem;
    std::map<std::string, Record> recordings;
    std::map<std::string, Record> people;
    std::map<std::string, Record> settings;
    std::set<std::string> unknown_people;
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
