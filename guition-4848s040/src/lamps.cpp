#include "lamps.h"
#include <AsyncUDP.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lwip/sockets.h>
#include <ctime>
#include <memory>
#include "tuya_proto.h"

namespace lamps {

namespace {

const char *CFG_FILE = "/tuya.cfg";
constexpr uint32_t LIVE_INTERVAL_MS = 50;     // Fader: höchstens 20 Befehle/s je Lampe
constexpr uint32_t PERSIST_DELAY_MS = 600;    // Endwert speichern, wenn der Fader ruht
constexpr uint32_t LOCAL_HOLD_MS = 1500;      // so lange keine Rückmeldung ins Layout (Finger liegt auf)
constexpr uint32_t HEARTBEAT_MS = 10000;
constexpr uint32_t RETRY_MS = 5000;
constexpr int CONNECT_TIMEOUT_MS = 1500;

struct Lamp {
  std::string id, key, ip, name;
  tuya::State want, known;
  bool knownValid = false;
  bool dirtySwitch = false, dirtyLive = false, dirtyPersist = false;
  bool reconnect = false;  // IP hat sich geändert
  uint32_t lastLocal = 0, lastLive = 0, lastHb = 0, nextConnect = 0;
  // nur im Task benutzt:
  int sock = -1;
  uint32_t seq = 1;
  std::string rx;
  bool reportedOffline = false;
};

SemaphoreHandle_t mtx = nullptr;
struct Lock {
  Lock() {
    // beim ersten Zugriff anlegen: activateLayout() ruft resendState() schon vor begin() auf
    if (!mtx) mtx = xSemaphoreCreateMutex();
    xSemaphoreTake(mtx, portMAX_DELAY);
  }
  ~Lock() { xSemaphoreGive(mtx); }
};

std::vector<std::unique_ptr<Lamp>> lampsList;  // geschützt durch mtx (Liste und Felder ohne "nur im Task")
std::vector<std::unique_ptr<Lamp>> pending;    // neue Liste, übernimmt der Task
bool pendingSet = false;
std::vector<osc::Message> feedbackQ;
std::vector<String> logQ;
AsyncUDP discovery;
bool ntpStarted = false;

void logLine(const String &s) {  // aufrufen mit gehaltenem Lock
  if (logQ.size() < 50) logQ.push_back(s);
}

String lampLabel(int i, const Lamp &l) { return "Lampe " + String(i + 1) + " (" + l.name.c_str() + ")"; }

// --- Datei ------------------------------------------------------------------------------------

std::vector<std::unique_ptr<Lamp>> parseConfig(const String &text, String &err) {
  std::vector<std::unique_ptr<Lamp>> out;
  int start = 0, lineNo = 0;
  while (start < (int)text.length()) {
    int end = text.indexOf('\n', start);
    if (end < 0) end = text.length();
    String line = text.substring(start, end);
    start = end + 1;
    lineNo++;
    line.trim();
    if (line.isEmpty()) continue;
    String f[4];
    int p = 0;
    for (int k = 0; k < 4; k++) {
      int t = line.indexOf('\t', p);
      f[k] = t < 0 ? line.substring(p) : line.substring(p, t);
      p = t < 0 ? line.length() : t + 1;
    }
    auto l = std::make_unique<Lamp>();
    l->id = f[0].c_str();
    l->key = f[1].c_str();
    l->ip = f[2].c_str();
    l->name = f[3].isEmpty() ? f[0].c_str() : f[3].substring(0, 40).c_str();
    IPAddress ip;
    if (l->id.empty() || l->id.size() > 40) err = "Zeile " + String(lineNo) + ": Geräte-ID fehlt";
    else if (l->key.size() != 16) err = "Zeile " + String(lineNo) + ": Schlüssel muss 16 Zeichen haben";
    else if (!l->ip.empty() && !ip.fromString(l->ip.c_str())) err = "Zeile " + String(lineNo) + ": IP ungültig";
    if (!err.isEmpty()) return {};
    if (out.size() >= 16) {
      err = "höchstens 16 Lampen";
      return {};
    }
    out.push_back(std::move(l));
  }
  return out;
}

// --- Netzwerk (nur im Task) -------------------------------------------------------------------

void closeLamp(Lamp &l) {
  if (l.sock >= 0) close(l.sock);
  l.sock = -1;
  l.rx.clear();
}

bool connectLamp(Lamp &l, const std::string &ip) {
  int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s < 0) return false;
  int fl = fcntl(s, F_GETFL, 0);
  fcntl(s, F_SETFL, fl | O_NONBLOCK);
  sockaddr_in a = {};
  a.sin_family = AF_INET;
  a.sin_port = htons(tuya::PORT);
  inet_pton(AF_INET, ip.c_str(), &a.sin_addr);
  int r = ::connect(s, (sockaddr *)&a, sizeof a);
  if (r < 0 && errno != EINPROGRESS) {
    close(s);
    return false;
  }
  if (r < 0) {
    fd_set w;
    FD_ZERO(&w);
    FD_SET(s, &w);
    timeval tv = {0, CONNECT_TIMEOUT_MS * 1000};
    int err = 0;
    socklen_t len = sizeof err;
    if (select(s + 1, nullptr, &w, nullptr, &tv) <= 0 ||
        getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &len) < 0 || err) {
      close(s);
      return false;
    }
  }
  int one = 1;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  l.sock = s;
  l.rx.clear();
  return true;
}

bool sendCmd(Lamp &l, uint32_t cmd, const std::string &json) {
  std::string pkt = tuya::pack(l.key, l.seq++, cmd, json);
  size_t off = 0;
  uint32_t t0 = millis();
  while (off < pkt.size()) {
    int n = send(l.sock, pkt.data() + off, pkt.size() - off, 0);
    if (n > 0) off += n;
    else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && millis() - t0 < 200) vTaskDelay(1);
    else return false;
  }
  return true;
}

std::string timestamp() {
  time_t t = time(nullptr);
  return t > 1600000000 ? std::to_string((long)t) : "0";  // Lampen akzeptieren auch "0"
}

bool sendDps(Lamp &l, const std::string &dps) {
  return sendCmd(l, tuya::CONTROL,
                 "{\"devId\":\"" + l.id + "\",\"uid\":\"" + l.id + "\",\"t\":\"" + timestamp() +
                     "\",\"dps\":{" + dps + "}}");
}

void service(int idx, Lamp &l, uint32_t now) {
  // Verbinden
  std::string ip;
  {
    Lock lk;
    if (l.reconnect) {
      l.reconnect = false;
      closeLamp(l);
      l.nextConnect = 0;
    }
    ip = l.ip;
  }
  if (l.sock < 0) {
    if (ip.empty() || (int32_t)(now - l.nextConnect) < 0) return;
    bool ok = connectLamp(l, ip);
    Lock lk;
    if (!ok) {
      l.nextConnect = millis() + RETRY_MS;
      if (!l.reportedOffline) logLine(lampLabel(idx, l) + " nicht erreichbar unter " + ip.c_str());
      l.reportedOffline = true;
      return;
    }
    if (l.reportedOffline) logLine(lampLabel(idx, l) + " wieder erreichbar");
    l.reportedOffline = false;
    l.lastHb = now;
    std::string q = "{\"gwId\":\"" + l.id + "\",\"devId\":\"" + l.id + "\",\"uid\":\"" + l.id +
                    "\",\"t\":\"" + timestamp() + "\"}";
    if (!sendCmd(l, tuya::DP_QUERY, q)) closeLamp(l);
    return;
  }

  // Empfangen
  char buf[512];
  for (;;) {
    int n = recv(l.sock, buf, sizeof buf, 0);
    if (n > 0) {
      l.rx.append(buf, n);
      if (l.rx.size() > 8192) l.rx.clear();
      continue;
    }
    if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
      closeLamp(l);
      l.nextConnect = now + 1000;  // Lampe hat getrennt: gleich neu verbinden
      return;
    }
    break;
  }
  tuya::Frame fr;
  std::string err;
  while (tuya::takeFrame(l.rx, l.key, fr, err)) {
    Lock lk;
    if (!err.empty()) {
      logLine(lampLabel(idx, l) + ": " + err.c_str());
      continue;
    }
    if (fr.json.find("\"dps\"") == std::string::npos) continue;
    tuya::fromStatus(fr.json, l.known);
    l.knownValid = true;
    if (now - l.lastLocal > LOCAL_HOLD_MS && !l.dirtyPersist && !l.dirtySwitch) {
      l.want = l.known;  // Änderung aus der App: Layout nachziehen
      for (auto &m : tuya::feedback(idx + 1, l.known))
        if (feedbackQ.size() < 200) feedbackQ.push_back(m);
    }
  }

  // Senden: was ist fällig?
  std::vector<std::string> todo;
  {
    Lock lk;
    if (l.dirtySwitch) {
      todo.push_back(tuya::switchDps(l.want));
      l.dirtySwitch = false;
      l.known.on = l.want.on;
    }
    if (l.dirtyLive && now - l.lastLive >= LIVE_INTERVAL_MS) {
      if (l.want.on) todo.push_back(tuya::controlData(l.want));
      l.dirtyLive = false;
      l.lastLive = now;
    }
    if (l.dirtyPersist && !l.dirtyLive && now - l.lastLocal >= PERSIST_DELAY_MS) {
      if (!l.knownValid || l.known.colour != l.want.colour) todo.push_back(tuya::modeDps(l.want));
      todo.push_back(tuya::persistDps(l.want));
      l.dirtyPersist = false;
      l.known.colour = l.want.colour;
    }
  }
  for (auto &dps : todo)
    if (!sendDps(l, dps)) {
      closeLamp(l);
      Lock lk;
      l.dirtyPersist = l.dirtySwitch = true;  // nach dem Neuverbinden nachholen
      return;
    }
  if (now - l.lastHb >= HEARTBEAT_MS) {
    l.lastHb = now;
    if (!sendCmd(l, tuya::HEART_BEAT, "{\"gwId\":\"" + l.id + "\",\"devId\":\"" + l.id + "\"}")) closeLamp(l);
  }
}

void onDiscovery(AsyncUDPPacket &p);  // weiter unten

void task(void *) {
  for (;;) {
    {
      Lock lk;
      if (pendingSet) {
        for (auto &l : lampsList) closeLamp(*l);
        lampsList = std::move(pending);
        pending.clear();
        pendingSet = false;
      }
    }
    if (WiFi.status() != WL_CONNECTED) {
      for (auto &l : lampsList) closeLamp(*l);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (!ntpStarted) {
      // Erst jetzt läuft der Netzwerk-Stack: vorher stürzt AsyncUDP::listen ab
      if (discovery.listen(tuya::DISCOVERY_PORT)) discovery.onPacket(onDiscovery);
      static String gw;
      gw = WiFi.gatewayIP().toString();
      configTime(0, 0, gw.c_str(), "pool.ntp.org");
      ntpStarted = true;
    }
    // Die Liste ändert sich nur oben in diesem Task, daher hier ohne Lock durchlaufen
    for (size_t i = 0; i < lampsList.size(); i++) service((int)i, *lampsList[i], millis());
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void onDiscovery(AsyncUDPPacket &p) {
  std::string json, id, ip;
  if (!tuya::decodeDiscovery(p.data(), p.length(), json)) return;
  if (!tuya::jsonValue(json, "gwId", id) || !tuya::jsonValue(json, "ip", ip)) return;
  Lock lk;
  for (size_t i = 0; i < lampsList.size(); i++) {
    Lamp &l = *lampsList[i];
    if (l.id != id || l.ip == ip) continue;
    logLine(lampLabel((int)i, l) + ": neue IP " + ip.c_str() + (l.ip.empty() ? "" : " (vorher " + String(l.ip.c_str()) + ")"));
    l.ip = ip;
    l.reconnect = true;
  }
}

}  // namespace

void begin() {
  if (!mtx) mtx = xSemaphoreCreateMutex();
  String err;
  File f = LittleFS.open(CFG_FILE, "r");
  if (f) {
    lampsList = parseConfig(f.readString(), err);
    f.close();
  }
  xTaskCreatePinnedToCore(task, "tuya", 8192, nullptr, 1, nullptr, 0);
}

bool handle(const osc::Message &m, String &logText) {
  std::vector<int> targets;
  std::string param, err;
  Lock lk;
  if (!tuya::parsePath(m.path, lampsList.size(), targets, param, err)) return false;
  String vals;
  for (auto &a : m.args) vals += " " + String(a.asString().c_str());
  if (err.empty()) {
    uint32_t now = millis();
    for (int i : targets) {
      Lamp &l = *lampsList[i];
      if (!tuya::apply(l.want, param, m, err)) break;
      if (param == "on") l.dirtySwitch = true;
      else l.dirtyLive = l.dirtyPersist = true;
      l.lastLocal = now;
    }
  }
  logText = String(m.path.c_str()) + vals + (err.empty() ? "  → Lampe" : String("  (") + err.c_str() + ")");
  return true;
}

void loop(const std::function<void(const osc::Message &)> &onFeedback,
          const std::function<void(const String &)> &onLog) {
  std::vector<osc::Message> fb;
  std::vector<String> logs;
  {
    Lock lk;
    fb.swap(feedbackQ);
    logs.swap(logQ);
  }
  for (auto &l : logs) onLog(l);
  for (auto &m : fb) onFeedback(m);
}

bool configure(const String &lines, String &err) {
  auto list = parseConfig(lines, err);
  if (!err.isEmpty()) return false;
  File f = LittleFS.open(CFG_FILE, "w");
  if (!f || f.print(lines) != lines.length()) {
    err = "Speichern fehlgeschlagen";
    return false;
  }
  f.close();
  Lock lk;
  pending = std::move(list);
  pendingSet = true;
  return true;
}

void resendState() {
  Lock lk;
  for (size_t i = 0; i < lampsList.size(); i++)
    if (lampsList[i]->knownValid)
      for (auto &m : tuya::feedback((int)i + 1, lampsList[i]->want))
        if (feedbackQ.size() < 200) feedbackQ.push_back(m);
}

std::vector<Info> list() {
  std::vector<Info> out;
  Lock lk;
  auto &src = pendingSet ? pending : lampsList;
  for (auto &l : src) {
    Info i;
    i.name = l->name.c_str();
    i.ip = l->ip.c_str();
    i.idTail = l->id.size() > 4 ? l->id.substr(l->id.size() - 4).c_str() : l->id.c_str();
    i.online = !pendingSet && l->sock >= 0;
    out.push_back(i);
  }
  return out;
}

}  // namespace lamps
