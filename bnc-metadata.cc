#include <cassert>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <pugixml.hpp>
#include <sqlite3.h>

using namespace std::string_literals;
using namespace boost::filesystem;
using namespace pugi;


struct DbError {
    DbError(std::string msg_) : msg{msg_} {}
    const std::string msg;

    std::string what() const {
        return msg;
    }
};


class DbStmt {
public:
    DbStmt(std::string sql_, sqlite3* db_) : sql{sql_}, db{db_}
    {
        stmt = NULL;
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL)) {
            error();
        }
        bind_index = 1;
    }

    ~DbStmt() {
        if (sqlite3_finalize(stmt)) {
            error();
        }
    }

    void exec() {
        int result = sqlite3_step(stmt);
        if (result == SQLITE_DONE) {
            // success
        } else if (result == SQLITE_ROW) {
            error("too many rows available");
        } else {
            error();
        }
    }

    DbStmt& bind(std::string v) {
        if (sqlite3_bind_text(stmt, bind_index, v.c_str(), -1, SQLITE_TRANSIENT)) {
            error();
        }
        ++bind_index;
        return *this;
    }

    DbStmt& bind(int v) {
        if (sqlite3_bind_int(stmt, bind_index, v)) {
            error();
        }
        ++bind_index;
        return *this;
    }

private:
    void error() {
        error(sqlite3_errmsg(db));
    }

    void error(std::string msg) {
        throw DbError(msg + " -- statement: " + sql);
    }

    std::string sql;
    int bind_index;
    sqlite3_stmt *stmt;
    sqlite3* db;
};


class Db {
public:
    Db(std::string filename_, int flags) : filename{filename_}
    {
        db = NULL;
        if (sqlite3_open_v2(filename.c_str(), &db, flags, NULL)) {
            error();
        }
    }

    ~Db() {
        if (sqlite3_close(db)) {
            error();
        }
    }

    DbStmt prepare(std::string sql) {
        return DbStmt(sql, db);
    }

    void exec(std::string sql) {
        prepare(sql).exec();
    }

private:
    void error() {
        throw DbError(filename + ": " + sqlite3_errmsg(db));
    }

    const std::string filename;
    sqlite3* db;
};


void create_db(Db& db) {
    db.exec(
        "CREATE TABLE IF NOT EXISTS person ("
            "fileid TEXT NOT NULL,"
            "personid TEXT NOT NULL,"
            "ageGroup TEXT,"
            "age TEXT,"
            "dialect TEXT,"
            "dialectDetail TEXT,"
            "role TEXT,"
            "sex TEXT,"
            "occupation TEXT,"
            "soc TEXT,"
            "persName TEXT,"
            "PRIMARY KEY (fileid, personid)"
        ")"
    );
    db.exec(
        "CREATE TABLE IF NOT EXISTS setting ("
            "fileid TEXT NOT NULL,"
            "settingid TEXT NOT NULL,"
            "activity TEXT,"
            "locale TEXT,"
            "placeName TEXT,"
            "who TEXT,"
            "PRIMARY KEY (fileid, settingid)"
        ")"
    );
}

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

    void store(Db& db, std::string stem, std::string table, std::string column, std::string key) {
        std::vector<std::string> columns;
        std::vector<std::string> values;
        columns.push_back("fileid");
        values.push_back(stem);
        columns.push_back(column);
        values.push_back(key);
        for (auto p : param) {
            columns.push_back(p.first);
            values.push_back(p.second);
        }
        std::string sql = "INSERT INTO "s + table;
        sql += " (" + boost::algorithm::join(columns, ", ") + ")";
        sql += " VALUES (";
        for (int i = 0; i < columns.size(); ++i) {
            if (i) {
                sql += ",?";
            } else {
                sql += "?";
            }
        }
        sql += ")";
        DbStmt stmt = db.prepare(sql);
        for (auto v : values) {
            stmt.bind(v);
        }
        stmt.exec();
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

    void store(Db& db) {
        std::vector<std::pair<std::string, std::string>> setting_person;
        for (std::string setting : seen_settings) {
            if (!settings.count(setting)) {
                std::cerr << stem << ": " << setting << ": unknown setting" << std::endl;
            }
            settings[setting].store(db, stem, "setting", "settingid", setting);
            std::string who = settings[setting].param["who"];
            if (who.size()) {
                std::vector<std::string> who_l;
                boost::split(who_l, who, boost::is_space());
                for (auto person : who_l) {
                    setting_person.push_back(std::make_pair(setting, person));
                    seen_people.insert(person);
                }
            }
        }
        for (std::string person : seen_people) {
            if (!people.count(person)) {
                std::cerr << stem << ": " << person << ": unknown person" << std::endl;
            }
            people[person].store(db, stem, "person", "personid", person);
        }
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
            setting = n;
        }

        for (xml_node u : rec.children()) {
            assert(u.name() == "u"s);
            parse_u(setting, u);
        }
    }

    void parse_u(std::string setting, xml_node u) {
        std::string who = u.attribute("who").value();
        assert(who.size() > 0);
        for (xml_node s : u.children("s")) {
            parse_s(setting, who, s);
        }
    }

    void parse_s(std::string setting, std::string who, xml_node s) {
        std::string n = s.attribute("n").value();
        seen_settings.insert(setting);
        seen_people.insert(who);
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
    std::set<std::string> seen_people;
    std::set<std::string> seen_settings;
};


bool process(Db& db, const path& p) {
    std::string stem = p.stem().string();
    xml_document doc;
    xml_parse_result result = doc.load_file(p.c_str());
    if (!result) {
        std::cerr << stem << ": error: " << result.description() << std::endl;
        return false;
    }
    File file(stem);
    file.parse(doc);
    file.store(db);
    return true;
}


bool process_all(Db& db, const path& p) {
    if (p.has_extension() && p.extension().string() == ".xml") {
        return process(db, p);
    } else if (is_directory(p)) {
        bool ok = true;
        std::vector<path> dir;
        for (auto& f : directory_iterator(p)) {
            dir.push_back(f.path());
        }
        std::sort(dir.begin(), dir.end());
        for (auto& f : dir) {
            if (!process_all(db, f)) {
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
        Db db("bnc.db", SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
        create_db(db);
        for (int i = 1; i < argc; ++i) {
            process_all(db, argv[i]);
        }
    }
    catch (filesystem_error e) {
        std::cerr << e.what() << std::endl;
        exit(1);
    }
    catch (DbError e) {
        std::cerr << e.what() << std::endl;
        exit(1);
    }
}
