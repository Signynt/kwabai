#include "events.h"
#include <QJsonDocument>
#include <iostream>
#include <stdexcept>
using namespace kwabai;
void check(bool ok){if(!ok)throw std::runtime_error("event assertion failed");}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::invalid_argument &){caught=true;}check(caught);}
int main(){try{
 EventJournal journal("bridge-a");EventCursor cursor;cursor.baseline("bridge-a",0);
 auto feed=[&](QJsonObject batch){return cursor.accept(QJsonDocument(batch).toJson());};
 check(feed(journal.since("bridge-a",0)));journal.append("window_added",{{"uuid","window-a"}});journal.append("window_drag_start");journal.append("window_drag_step");journal.append("window_drag_finish");
 auto batch=journal.since("bridge-a",0);check(feed(batch));check(cursor.cursor()==4);check(cursor.events()["events"].toArray().size()==4);
 rejects([&]{feed(batch);});check(cursor.cursor()==4);check(feed(journal.since("bridge-a",4)));check(cursor.events()["events"].toArray().size()==4);
 journal.append("window_closed");batch=journal.since("bridge-a",4);auto bad=batch;auto records=bad["events"].toArray();auto event=records[0].toObject();event["ordinal"]="6";records[0]=event;bad["events"]=records;
 rejects([&]{feed(bad);});check(cursor.cursor()==4);check(feed(batch));check(cursor.cursor()==5);
 for(int i=0;i<300;i++)journal.append("window_geometry");check(journal.since("bridge-a",5)["resync-required"].toBool());check(!feed(journal.since("bridge-a",5)));check(cursor.cursor()==5);
 cursor.baseline("bridge-a",journal.latest());check(feed(journal.since("bridge-a",cursor.cursor())));check(cursor.events()["events"].toArray().isEmpty());
 check(!feed(journal.since("bridge-b",cursor.cursor())));check(journal.since("bridge-a",journal.latest()+1)["resync-required"].toBool());
 rejects([&]{journal.append("execute_shell");});check(journal.latest()==305);
 journal.append("window_focus");batch=journal.since("bridge-a",305);bad=batch;bad["latest"]="307";rejects([&]{feed(bad);});check(cursor.cursor()==305);
 check(feed(batch));cursor.clear();check(!cursor.status()["ready"].toBool());rejects([&]{feed(batch);});
 std::cout<<"PASS event ordering, atomic validation, bounded retention, overflow resync and restart isolation\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
