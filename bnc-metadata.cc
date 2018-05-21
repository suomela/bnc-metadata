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
            warning();
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
        if (sqlite3_reset(stmt)) {
            error();
        }
        bind_index = 1;
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
    void error() const {
        error(sqlite3_errmsg(db));
    }

    void error(std::string msg) const {
        throw DbError(msg + " -- statement: " + sql);
    }

    void warning() const {
        std::cerr << sqlite3_errmsg(db) << std::endl;
    }

    const std::string sql;
    int bind_index;
    sqlite3_stmt* stmt;
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
            warning();
        }
    }

    DbStmt prepare(std::string sql) const {
        return DbStmt(sql, db);
    }

    void exec(std::string sql) const {
        prepare(sql).exec();
    }

private:
    void error() const {
        throw DbError(filename + ": " + sqlite3_errmsg(db));
    }

    void warning() const {
        std::cerr << filename << ": " << sqlite3_errmsg(db) << std::endl;
    }

    const std::string filename;
    sqlite3* db;
};


void create_db(Db& db) {
    db.exec(
        "CREATE TABLE bnc_person ("
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
        "CREATE TABLE bnc_setting ("
            "fileid TEXT NOT NULL,"
            "settingid TEXT NOT NULL,"
            "activity TEXT,"
            "locale TEXT,"
            "placeName TEXT,"
            "who TEXT,"
            "PRIMARY KEY (fileid, settingid)"
        ")"
    );
    db.exec(
        "CREATE TABLE bnc_setting_person ("
            "fileid TEXT NOT NULL,"
            "settingid TEXT NOT NULL,"
            "personid TEXT NOT NULL,"
            "PRIMARY KEY (fileid, personid, settingid),"
            "FOREIGN KEY (fileid, settingid) REFERENCES bnc_setting(fileid, settingid),"
            "FOREIGN KEY (fileid, personid) REFERENCES bnc_person(fileid, personid)"
        ")"
    );
    db.exec(
        "CREATE TABLE bnc_s ("
            "fileid TEXT NOT NULL,"
            "n TEXT NOT NULL,"
            "personid TEXT NOT NULL,"
            "settingid TEXT NOT NULL,"
            "n_w INTEGER NOT NULL,"
            "n_c INTEGER NOT NULL,"
            "n_unclear INTEGER NOT NULL,"
            "n_vocal INTEGER NOT NULL,"
            "n_gap INTEGER NOT NULL,"
            "PRIMARY KEY (fileid, n, personid),"
            "FOREIGN KEY (fileid, settingid) REFERENCES bnc_setting(fileid, settingid),"
            "FOREIGN KEY (fileid, personid) REFERENCES bnc_person(fileid, personid)"
        ")"
    );
    db.exec(
        "CREATE TABLE bnc_w ("
            "fileid TEXT NOT NULL,"
            "n TEXT NOT NULL,"
            "personid TEXT NOT NULL,"
            "wordid INTEGER NOT NULL,"
            "settingid TEXT NOT NULL,"
            "hw TEXT NOT NULL,"
            "c5 TEXT NOT NULL,"
            "pos TEXT NOT NULL,"
            "PRIMARY KEY (fileid, n, personid, wordid),"
            "FOREIGN KEY (fileid, n, personid) REFERENCES bnc_s(fileid, n, personid),"
            "FOREIGN KEY (fileid, settingid) REFERENCES bnc_setting(fileid, settingid),"
            "FOREIGN KEY (fileid, personid) REFERENCES bnc_person(fileid, personid)"
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

    void store(Db& db, std::string stem, std::string table, std::string column, std::string key) const {
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


struct Word {
    std::string hw;
    std::string c5;
    std::string pos;
};

struct Wordcount : public xml_tree_walker {
    int w = 0;
    int c = 0;
    int unclear = 0;
    int vocal = 0;
    int gap = 0;
    std::vector<Word> words;

    virtual bool for_each(xml_node& node) {
        if (node.type() == node_element) {
            if (node.name() == "w"s) {
                Word word {
                    node.attribute("hw").value(),
                    node.attribute("c5").value(),
                    node.attribute("pos").value(),
                };
                words.push_back(word);
                ++w;
            } else if (node.name() == "c"s) {
                ++c;
            } else if (node.name() == "unclear"s) {
                ++unclear;
            } else if (node.name() == "vocal"s) {
                ++vocal;
            } else if (node.name() == "gap"s) {
                ++gap;
            }
        }
        return true;
    }

    bool nonempty() const {
        return w || c || unclear || vocal || gap;
    }
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
        store_setting(db);
        store_person(db);
        store_setting_person(db);
        store_s(db);
    }

private:
    void store_setting(Db& db) {
        for (std::string setting : seen_settings) {
            if (!settings.count(setting)) {
                std::cerr << stem << ": " << setting << ": unknown setting" << std::endl;
            }
            const Record& r = settings[setting];
            r.store(db, stem, "bnc_setting", "settingid", setting);
            if (r.param.count("who")) {
                std::string who = r.param.at("who");
                assert(who.size());
                std::vector<std::string> who_l;
                boost::split(who_l, who, boost::is_space());
                for (auto person : who_l) {
                    setting_person.push_back(std::make_pair(setting, person));
                    seen_people.insert(person);
                }
            }
        }
    }

    void store_person(Db& db) {
        for (std::string person : seen_people) {
            if (!people.count(person) && person != "PS000"s && person != "PS001") {
                std::cerr << stem << ": " << person << ": unknown person" << std::endl;
            }
            const Record& r = people[person];
            r.store(db, stem, "bnc_person", "personid", person);
        }
    }

    void store_setting_person(Db& db) const {
        DbStmt stmt = db.prepare(
            "INSERT INTO bnc_setting_person "
            "(fileid, settingid, personid)"
            "VALUES (?,?,?)"
        );
        for (const auto& p : setting_person) {
            stmt.bind(stem).bind(p.first).bind(p.second).exec();
        }
    }

    void store_s(Db& db) const {
        DbStmt stmt = db.prepare(
            "INSERT INTO bnc_s "
            "(fileid, n, settingid, personid, n_w, n_c, n_unclear, n_vocal, n_gap)"
            "VALUES (?,?,?,?,?,?,?,?,?)"
        );
        DbStmt stmt_w = db.prepare(
            "INSERT INTO bnc_w "
            "(fileid, n, settingid, wordid, personid, hw, c5, pos)"
            "VALUES (?,?,?,?,?,?,?,?)"
        );
        for (const auto& t : s_tags) {
            const auto& wc = std::get<3>(t);
            stmt.bind(stem);
            stmt.bind(std::get<0>(t));
            stmt.bind(std::get<1>(t));
            stmt.bind(std::get<2>(t));
            stmt.bind(wc.w);
            stmt.bind(wc.c);
            stmt.bind(wc.unclear);
            stmt.bind(wc.vocal);
            stmt.bind(wc.gap);
            stmt.exec();
            for (int i = 0; i < wc.words.size(); ++i) {
                stmt_w.bind(stem);
                stmt_w.bind(std::get<0>(t));
                stmt_w.bind(std::get<1>(t));
                stmt_w.bind(i);
                stmt_w.bind(std::get<2>(t));
                stmt_w.bind(wc.words[i].hw);
                stmt_w.bind(wc.words[i].c5);
                stmt_w.bind(wc.words[i].pos);
                stmt_w.exec();
            }
        }
    }

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
        Wordcount wc;
        s.traverse(wc);
        if (wc.nonempty()) {
            seen_settings.insert(setting);
            seen_people.insert(who);
            s_tags.push_back(std::make_tuple(n, setting, who, wc));
        }
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
            assert(p.id.size());
            assert(target.count(p.id) == 0);
            target[p.id] = p;
            if (p.n.size()) {
                assert(target.count(p.n) == 0);
                target[p.n] = p;
            }
        }
    }

    const std::string stem;
    std::map<std::string, Record> recordings;
    std::map<std::string, Record> people;
    std::map<std::string, Record> settings;
    std::set<std::string> seen_people;
    std::set<std::string> seen_settings;
    std::vector<std::pair<std::string, std::string>> setting_person;
    std::vector<std::tuple<std::string, std::string, std::string, Wordcount>> s_tags;
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
        db.exec("PRAGMA foreign_keys = ON");
        db.exec("BEGIN");
        create_db(db);
        for (int i = 1; i < argc; ++i) {
            process_all(db, argv[i]);
        }
        db.exec("COMMIT");
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
