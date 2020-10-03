/*
  13:00
*/

#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>
#include <vector>
#include <string>
#include <iostream>
#include <mutex>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SHOUT_PORT 6969
#define SHOUT_LSTN 6968
#define SRV_PORT 6967
#define CLN_PORT 6966
#define _SRV "server"
#define _CLN "client"
#define NAME "winter"

#define CMD_SHOOT "shoot "
#define CMD_AUTO "autoshoot"
#define CMD_EXIT "exit"

#define S_BCAST "ifconfig | grep -o \"broadcast.*\" | grep -o \"[0-9].*\" | tr \"\\n\" \"\\0\""

namespace Shout {
  size_t get(std::string payload, char *&buf) {
    size_t len = strlen(NAME) + 5 + payload.length();

    buf = new char[len];

    size_t offset = strlen(NAME);
    int t = payload.length();

    memcpy(buf, NAME, offset);
    buf[offset] = 0x01;

    memcpy(buf + offset + 1, &t, 4);
    memcpy(buf + offset + 5, payload.c_str(), t);

    return len;
  }
}

namespace util {
  struct msg {
    char op;
    std::string gameName;
    char *payload;
    short size;
  };

  signed char random_direction(int bias) {
      char r = (std::rand()/((RAND_MAX + 1u)/ 10));
      if (r > bias) return 1;
      if (r == bias) return 0;
      return -1;
  }

  in_addr bcast;
}

using namespace std;

/*
  announce msgs
  {winter} char[]
  char op == 0x01 : ANNOUNCE
  int size
  name (len = size)

*/

class Shouter {
  int sock;
  std::thread __t;
  bool run;
  std::string name;
  char* msg;
  size_t msg_len;
  sockaddr_in dst;

  void work() {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    if (::sendto(sock, msg, msg_len, 0, (sockaddr*) &dst, sizeof(dst)) < 0) {
      cout << "couldn't write to shout sock" << endl;
      cout << strerror(errno) << " (" << to_string(errno) << ")" << endl;
    }
  }

public:
  Shouter(int sock, std::string name) {
    this->msg_len = Shout::get(name, this->msg);
    this->sock = sock;
    this->run = false;

    dst.sin_port = htons(SHOUT_LSTN);
    dst.sin_family = AF_INET;
    memcpy(&dst.sin_addr, &util::bcast, sizeof(in_addr));
  }

  ~Shouter() {
    delete [] msg;
  }

  void start() {
    this->run = true;
    __t = std::thread(&Shouter::_run, this);
  }

  void _run() {
    while (this->run) {
      this->work();
    }
  }

  void stop() {
    this->run = false;
  }
};


/*
call syntax:
  winter server [name] [localip] [local.bcast]
  winter client
*/

int _err_syntax() {
  cout << "Invalid calling syntax!" << endl << "Try:" << endl
  << NAME << " server [name]" << endl
  << NAME << " client" << endl;

  return 1;
}

namespace mq {
  struct link {
    void *obj;
    link *next;
  };
}

class mqueue {
  mq::link *top;
  mq::link *bottom;

  size_t _link_count;
public:
  mqueue() {
    top = NULL;
    bottom = NULL;
    _link_count = 0;
  }

  void addItem(void* item) {
    mq::link *link = new mq::link;

    link->obj = item;
    link->next = NULL;

    if (bottom == NULL) {
      top = bottom = link;
      _link_count = 1;
      return;
    }

    if (bottom == top) {
      top->next = link;
      bottom = link;
      _link_count = 2;
      return;
    }

    bottom->next = link;
    bottom = link;
    _link_count++;
  }


  void *nextItem() {
    mq::link *link = top;
    void *obj = top->obj;

    if (link == bottom) {
      bottom = top = NULL;
    } else {
      top = link->next;
    }

    _link_count--;
    delete link;

    return obj;
  }
  bool hasNext() {
    return _link_count > 0;
  }
};

/**
  general msg layout
  winter: byte x 6
  op: byte x 1
  byte: size of game-name
  byte[]: game name
  int32: size of rest of packet
  byte[]: rest of packet

  optional extra

  at minnimum a packet read should have at least 11 bytes

  op codes

  0x01 announce
  0x05 shoot
  0x06 hit
  0x07 miss
  0x09 zombie win

**/

class Server {
  int sock;
  bool running;
  string name;

  std::mutex r_mtx;
  mqueue _rq;
  std::thread __read;

  std::mutex w_mtx;
  mqueue _wq;
  std::thread __write;

  signed char _zx, _zy;
  sockaddr_in dst;


public:
  Server(int sock, string name) {
    this->sock = sock;
    this->name = name;

    std::srand(std::time(nullptr));

    _zy = 0;
    _zx = std::rand()/((RAND_MAX + 1u) / 10);
    //_zx = 0;
    dst.sin_port = htons(CLN_PORT);
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = INADDR_BROADCAST;
  }

  void readLoop() {
    while (running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(75));
      _read();
    }
  }

  void writeLoop() {
    while (running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(75));
      _write();
    }
  }

  void _write() {
    util::msg *msg;
    w_mtx.lock();

    if (_wq.hasNext()) {
      msg = (util::msg *) _wq.nextItem();
    } else {
      msg = NULL;
    }

    w_mtx.unlock();

    if (msg == NULL) return;

    cout << "writing a msg" << endl;

    char _buf[2048];
    size_t offset = strlen(NAME);
    char _name_size = this->name.length();

    memcpy(&_buf, NAME, offset);

    _buf[offset] = msg->op;

    _buf[offset + 1] = _name_size;

    memcpy(_buf + offset + 2, this->name.c_str(), _name_size);

    offset += (_name_size + 2);

    memcpy(_buf + offset, &msg->size, sizeof(short));
    memcpy(_buf + offset + sizeof(short), msg->payload, msg->size);

    offset += sizeof(short) + msg->size;

    ::sendto(sock, &_buf, offset + 1, 0, (sockaddr*)&dst, sizeof(sockaddr_in));

    delete [] msg->payload;
    delete msg;
  }

  void _read() {
    char _buf[2048];
    size_t __read, offset; // we should check what payload_size ends up being less than (2048 - 11 - name_size)
    short int _payload_size;
    char op;

    util::msg *msg = new util::msg;

    char _name_size;
    char *name, *payload;

    // read up to max of 2048 bytes

    __read = ::read(sock, &_buf, 2048);

    if (__read < 11) return;

    // if we read less than  11 bytes this could not be a valid msg

    offset = strlen(NAME);

    // if the first bytes are not the name of the program, this is not a valid msg

    if (memcmp(_buf, NAME, offset) != 0) return;
    msg->op = _buf[offset];

    _name_size = _buf[offset + 1];

    // if we didn't read enough to read the game name then msg is invalid

    if (__read < 11 + _name_size) return;

    // if the msg gameName is not the same as our game name drop the msg

    if (memcmp(_buf + offset + 2, this->name.c_str(), _name_size)) return;

    name = new char[_name_size];

    memcpy(name, _buf + offset + 2, _name_size);

    msg->gameName = string(name, _name_size);

    delete [] name;

    offset += (2 + _name_size);

    memcpy(&_payload_size, _buf + offset, sizeof(short));

    //_payload_size = *reinterpret_cast<short*>(&_buf + offset);
    payload = new char[_payload_size];

    // this calculation is wrong

    if (__read < 11 + _name_size + _payload_size) return;

    memcpy(payload, _buf + offset + sizeof(short), _payload_size);

    msg->size = _payload_size;
    msg->payload = payload;

    r_mtx.lock();

    _rq.addItem((void*) msg);

    r_mtx.unlock();
  }

  void start() {
    running = true;
    __read = std::thread(&Server::readLoop, this);
    __write = std::thread(&Server::writeLoop, this);

    workLoop();
  }

  void workLoop() {
    int counter = 0;
    util::msg *w_msg;
    util::msg *r_msg;


    while (running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      counter ++;

      r_mtx.lock();

      if (_rq.hasNext()) {
        r_msg = (util::msg *) _rq.nextItem();
      } else {
        r_msg = NULL;
      }

      r_mtx.unlock();

      if (r_msg != NULL) handle(r_msg);

      if (counter % 8 == 0) { // zombie will move ~1 time per second
        moveZombie();
      }
      if (counter >= 20) {
        announceZombie();
        counter = 0;
      }
    }
  }

  void hit(util::msg *old) {
    util::msg *msg = new util::msg;
    msg->payload = new char[old->size];

    memcpy(msg->payload, old->payload, old->size);
    msg->size = old->size;

    msg->op = 0x06;
    msg->gameName = name;

    w_mtx.lock();

    _wq.addItem((void*) msg);

    w_mtx.unlock();

    _zy = 0;
  }

  void miss(util::msg *old) {
    util::msg *msg = new util::msg;
    msg->payload = new char[old->size];

    memcpy(msg->payload, old->payload, old->size);
    msg->size = old->size;

    msg->op = 0x07;
    msg->gameName = name;

    w_mtx.lock();

    _wq.addItem((void*) msg);

    w_mtx.unlock();
  }

  void zombieWin() {
    util::msg *msg = new util::msg;
    msg->payload = new char[0];

    msg->op = 0x09;
    msg->gameName = name;
    msg->size = 0; // will zero payload msgs work? lets find out...

    w_mtx.lock();

    _wq.addItem((void*) msg);

    w_mtx.unlock();
  }

  void handle(util::msg *msg) {
    string client;
    short _len;
    unsigned char cx, cy;
    if (msg->op == 0x05) {
      if (msg->size > sizeof(short) || true) {
        memcpy(&_len, msg->payload, sizeof(short));
        client = string(msg->payload + 2, _len);
        cx = msg->payload[_len + sizeof(short)] - 1;
        cy = 30 - msg->payload[_len + sizeof(short) + 1];

        cout << "got a shot! from " << client << "|| len: " << to_string(_len) << endl;
        cout << "zombie: " << to_string(_zx) << ", " << to_string(_zy) << endl;
        cout << "shot: " << to_string(cx) << ", " << to_string(cy) << endl;

        if (cx == _zx && cy == _zy) {
          hit(msg);
        } else {
          miss(msg);
        }
      }
    } else {
      cout << "got some msg type that i don't know about" << endl;
    }
    delete [] msg->payload;
    delete msg;
  }

  void announceZombie() {
    util::msg *msg = new util::msg;

    msg->gameName = name;
    msg->op = 0x01;

    // we need two chars to announce the zombie position

    msg->size = 2;
    msg->payload = new char[2];
    msg->payload[0] = _zx;
    msg->payload[1] = _zy;

    w_mtx.lock();

    _wq.addItem(msg);

    w_mtx.unlock();
  }

  void moveZombie() {
    signed char mx, my;

    //r_mtx.lock(); // don't move the zombie if a msg is being handled. nope, checking shoots and moving zombie is done by the same thread

    mx = util::random_direction(5);
    my = util::random_direction(2); // more likely to be positive

    // return; // uncomment to freeze the zombie

    _zx += mx;
    _zy += my;

    if (_zx < 0) _zx = 0;
    if (_zx > 9) _zx = 9;

    if (_zy < 0) _zy = 0;
    //if (_zy > 30) _zy = 0; // change to set = 0 and announce winner = zombie
    //_zy = 31; // just casually forcing the zombie to win...

    if (_zy >= 30) {
      zombieWin();
      _zy = 0;
    }

    cout << "zombie is now at " << to_string(_zx) << ", " << to_string(_zy) << endl;

    //r_mtx.unlock();
  }
};

/**
so client reads from CLN_PORT
it also reads from cin


**/

class Client {
  int sock;
  string name;
  string game;
  std::thread __read;
  std::thread __work;
  bool running;
  int smsgc;
  int _zx, _zy;

  sockaddr_in dst;

  std::mutex w_mtx;
  mqueue _wq;

  void drawBoard() {
    //clearScreen();
    for (int i = 1; i < 20; i += 2) {
      setPos(i + 3, 0);
      cout << to_string(((i - 1) / 2) + 1);
      setPos(i + 4, 2);
      cout << "#";
    }
    for (int i = 0; i < 30; i++) {
      setPos(0, i + 3);
      cout << to_string(i + 1);
      for (int o = 0; o < 11; o++) {
        setPos(4 + o * 2, i + 3);
        cout << "|";
        setPos(3 + o * 2, i + 3);
        cout << " ";
      }
    }
    for (int i = 1; i < 20; i += 2) {
      setPos(i + 4, 33);
      cout << "#";
      setPos(i + 3, 34);
      cout << to_string(((i - 1) / 2) + 1);
    }
    setPos(0, 40);
  }

  void setPos(int x, int y) {
    printf("%c[%d;%df", 0x1B, y, x);
  }

  void savePos() {
    cout << "\033[s";
  }

  void restorePos() {
    cout << "\033[u";
  }

  void clearScreen() {
    cout << "\033[2J";
  }

  void drawShot(char x, char y) {
    savePos();
    x = 5 + ((x - 1) * 2);
    y = 2 + y;

    setPos(x, y);
    cout << "x";
    restorePos();
  }

  void drawZombie(char x, char y) {
    savePos();

    drawBoard();
/*
    setPos(0, 35);
    cout << "                             ";
    setPos(0, 35);
    cout << to_string(x) << ", " << to_string(y) << " =======";
*/

    x = 5 + (x * 2);
    y = 2 + (30 - y);

    _zx = x;
    _zy = y;
/*
    setPos(0, 39);
    cout << to_string(x) << ", " << to_string(y) << "      ";*/
    setPos(x, y);
    cout << "Z";

    restorePos();
  }

  void readLoop() {
    int rcount = 0;
    while (running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(75));
      /*
      savePos();
      setPos(0, 36);
      cout << "read " << rcount++;
      restorePos();*/
      _read();
    }
  }

  void _write() {
    util::msg *msg;

    w_mtx.lock();

    if (_wq.hasNext()) {
      msg = (util::msg *) _wq.nextItem();
      /*
      savePos();
      setPos(0, 42);
      cout << "send msg " << to_string(smsgc++) << " ||";
      restorePos();*/
    } else {
      msg = NULL;
    }

    w_mtx.unlock();

    if (msg == NULL) return;

    char _buf[2048];
    size_t offset = strlen(NAME);
    char _name_size = this->game.length();

    memcpy(&_buf, NAME, offset);

    _buf[offset] = msg->op;

    _buf[offset + 1] = _name_size;

    memcpy(_buf + offset + 2, this->game.c_str(), _name_size);

    offset += _name_size + 2;

    memcpy(_buf + offset, &msg->size, sizeof(short));
    memcpy(_buf + offset + sizeof(short), msg->payload, msg->size);

    offset += sizeof(short) + msg->size;

    ::sendto(sock, &_buf, offset + 1, 0, (sockaddr*)&dst, sizeof(dst));

    delete [] msg->payload;
    delete msg;
  }

  void _read() {
    char _buf[2048];
    size_t __read, offset;
    short int _payload_size;
    char op;

    char _name_size;
    char *payload;

    __read = ::read(sock, &_buf, 2048);

    if (__read < 0) {
      setPos(0, 50);
      cout << strerror(errno) << " (" << to_string(errno) << ")";
    } else {/*
      savePos();
      setPos(0, 37);
      cout << "read " << to_string(__read) << " bytes   ";
      restorePos();*/
    }

    if (__read < 11) return;

    offset = strlen(NAME);

    if (memcmp(_buf, NAME, offset) != 0) return;

    op = _buf[offset];
    _name_size = _buf[offset + 1];

    if (memcmp(_buf + offset + 2, game.c_str(), game.length()) != 0) {
      return;
    }

    offset += 2 + game.length();

    memcpy(&_payload_size, _buf + offset, sizeof(short));

    offset += sizeof(short);

    payload = new char[_payload_size];

    memcpy(payload, _buf + offset, _payload_size);
    /*
    savePos();
    setPos(0, 38);
    cout << "payload: " << to_string(payload[0]) << ", " << to_string(payload[1]);
    restorePos();
*/
    handle(op, _payload_size, payload); // remember to delete payload!
  }

  void handleHit(short length, char *payload) {
    short _len = 0;
    memcpy(&_len, payload, sizeof(short));
    string player;

    if (_len == name.length()) {
      if (memcmp(payload + sizeof(short), name.c_str(), _len) == 0) {
        savePos();
        setPos(0, 49);
        cout << "You won!!              ";
        restorePos();
        return;
      }
    }

    player = string(payload + sizeof(short), _len);
    savePos();
    setPos(0, 49);
    cout << player << " won!!                 ";
    restorePos();

  }

  void handleMiss(short length, char *payload) {
    short _len = 0;
    memcpy(&_len, payload, sizeof(short));
    string player;

    if (_len == name.length()) {
      if (memcmp(payload + sizeof(short), name.c_str(), _len) == 0) {
        savePos();
        setPos(0, 49);
        cout << "You missed!!                                           ";
        restorePos();
        return;
      }
    }

    player = string(payload + sizeof(short), _len);
    savePos();
    setPos(0, 49);
    cout << player << " missed!!                                       ";
    restorePos();
  }

  void handleZombieWin() {
    savePos();
    setPos(0, 49);
    cout << "Oh no the zombie has won the north is lost!";
    setPos(0, 50);
    cout << "Medal: you lost.";
    restorePos();
  }

  void handle(char op, short length, char *payload) {
    if (op == 0x01 && length == 2) { // announce zombie
      drawZombie(payload[0], payload[1]);
    }
    if (op == 0x07) {
      handleMiss(length, payload);
    }
    if (op == 0x06) {
      handleHit(length, payload);
    }
    if (op == 0x09) {
      handleZombieWin();
    }

    delete [] payload;
  }

  void workLoop() {
    while (running) {
      std::this_thread::sleep_for(std::chrono::milliseconds(75));
      _write();
    }
  }

public:
  Client(int sock, string name, string game) {
    this->sock = sock;
    this->name = name;
    this->game = game;
    smsgc = 0;

    dst.sin_port = htons(SRV_PORT);
    dst.sin_family = AF_INET;
    dst.sin_addr = util::bcast;
  }

  string prompt() {
    string line;

    setPos(0, 40);
    cout << "enter command:                                         ";
    setPos(16, 40);

    getline(cin, line);

    return line;
  }

  void handleCommand(string cmd) {
    if (memcmp(cmd.c_str(), CMD_SHOOT, strlen(CMD_SHOOT)) == 0) {
      handleShoot(cmd.substr(strlen(CMD_SHOOT), cmd.length() - strlen(CMD_SHOOT)));
    }
    if (memcmp(cmd.c_str(), CMD_AUTO, strlen(CMD_AUTO)) == 0) {
      handleShoot(to_string(_zx) + " " + to_string(_zy));
    }
    if (memcmp(cmd.c_str(), CMD_EXIT, strlen(CMD_EXIT)) == 0) {
      shutdown();
    }
  }

  void shutdown() {
    ::close(sock);
    running = false;
  }

  void handleShoot(string coord) {
    string x, y;
    char _x, _y;
    util::msg *msg;
    short _size;

    for (int i = 0; i < coord.length(); i++) {
      if (coord[i] == ' ') {
        x = coord.substr(0, i);
        y = coord.substr(i + 1);

        _x = stoi(x);
        _y = stoi(y);
        drawShot(_x, _y);

        /*
        setPos(0, 45);
        cout << "shooting " << to_string(_x) << ", " << to_string(_y);
*/
        msg = new util::msg;
        msg->op = 0x05;
        msg->gameName = this->game;

        _size = this->name.length();

        msg->size = _size + sizeof(short) + (2 * sizeof(char));
        msg->payload = new char[msg->size];

        memcpy(msg->payload, &_size, sizeof(short));

        memcpy(msg->payload + sizeof(short), this->name.c_str(), _size);
        msg->payload[_size + sizeof(short)] = _x;
        msg->payload[_size + sizeof(short) + 1] = _y;

        w_mtx.lock();

        _wq.addItem((void*) msg);

        w_mtx.unlock();

        return;
      }
    }
  }

  void start() {
    running = true;
    clearScreen();
    drawBoard();
    __read = std::thread(&Client::readLoop, this);
    __work = std::thread(&Client::workLoop, this);

    cout << std::unitbuf;

    setPos(0, 36);
    cout << "Welcome to the wall.";
    setPos(0, 37);
    cout << "It's our job to defend the wall against zombies.";
    setPos(0, 38);
    cout << "Look! There's the zombie!";
    setPos(0, 39);
    cout << "Type 'shoot [x] [y]' to shoot at his position.";

    while (running) {
      handleCommand(prompt());
    }
  }
};

int server_workloop(string name) {
  sockaddr_in l_addr;
  l_addr.sin_port = htons(SRV_PORT);
  l_addr.sin_family = AF_INET;
  l_addr.sin_addr.s_addr = INADDR_ANY;

  int sock = ::socket(AF_INET, SOCK_DGRAM, 0);

  int bc = 1;

  if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bc, sizeof(bc)) < 0) {
		cout << ("could not set options on shouty shout") << endl;
    return 1;
	}

  if (::bind(sock, (sockaddr*) &l_addr, sizeof(sockaddr_in)) < 0) {
    cout << "bind server in error" << endl << strerror(errno) << "(" << to_string(errno) << ")" << endl;
    return 1;
  }

  cout << "starting now" << endl;

  Server srv(sock, name);
  srv.start();

  return 0;
}


int client_workloop(string gameName, string name) {
  system("clear");
  cout << "found a game: " << gameName << endl;

  sockaddr_in l_addr;
  l_addr.sin_port = htons(CLN_PORT);
  l_addr.sin_family = AF_INET;
  l_addr.sin_addr.s_addr = INADDR_ANY;

  int sock = ::socket(AF_INET, SOCK_DGRAM, 0);

  int bc = 1;

  if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bc, sizeof(bc)) < 0) {
		cout << ("could not set options on shouty shout") << endl;
    return 1;
	}

  if (::bind(sock, (sockaddr*) &l_addr, sizeof(sockaddr_in)) < 0) {
    cout << "bind server in error" << endl << strerror(errno) << "(" << to_string(errno) << ")" << endl;
    return 1;
  }

  cout << "starting now" << endl;

  Client cli(sock, name, gameName);
  cli.start();
  return 0;
}

int init_server(std::string name) {
  sockaddr_in addr;
	addr.sin_port = htons(SHOUT_PORT);
	addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;


  int shout = ::socket(AF_INET, SOCK_DGRAM, 0);

  if (shout < 0) {
    cout << "could not open shouty shout" << endl;
    return 1;
  }

  int bc = 1;

  if (setsockopt(shout, SOL_SOCKET, SO_BROADCAST, &bc, sizeof(bc)) < 0) {
		cout << ("could not set options on shouty shout") << endl;
    return 1;
	}

  if (::bind(shout, (sockaddr*) &addr, sizeof(sockaddr_in)) < 0) {
    cout << "could not bind shouty socket " << endl << strerror(errno) << " (" << to_string(errno) << ")" << endl;
    return 1;
  }

  cout << "starting shouty" << endl;

  Shouter shouty(shout, name);
  shouty.start();

  cout << "shouty started" << endl;

  return server_workloop(name);
}

string _readGameName(int sock) {
  char _buf[2048];
  size_t _read;

  _read = ::read(sock, _buf, 2048);

  size_t offset = strlen(NAME);
  int psize;

  if (_read > offset + 5) {
    if (memcmp(_buf, NAME, offset) == 0) {
      if (_buf[offset] == 0x01) {
        memcpy(&psize, _buf + offset + 1, 4);
        return string(_buf + offset + 5, psize);
      } else {
        cout << "bad op" << endl;
      }
    } else {
      cout << "bad sig" << endl;
    }
  } else {
    cout << "way too small" << endl;
  }

  cout << "invalid packet" << endl;
  return "";
}

bool find_game(string &name) {
  sockaddr_in addr;
	addr.sin_port = htons(SHOUT_LSTN);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;

  int listen = ::socket(AF_INET, SOCK_DGRAM, 0);

  int bc = 1;

  cout << "will listen on :" << to_string(SHOUT_LSTN) << " until a server is found." << endl;

  if (listen < 0) {
    cout << "could not open shouty socket for listen." << endl;
    return false;
  }

  if (setsockopt(listen, SOL_SOCKET, SO_BROADCAST, &bc, sizeof(bc)) < 0) {
		cout << ("could not set options on shouty shout") << endl;
    return false;
	}

  if (::bind(listen, (sockaddr*) &addr, sizeof(sockaddr_in)) < 0) {
    cout << "bind error" << endl;
    cout << std::strerror(errno) << endl;
    return false;
  }

  bool found = false;
  string gameName;

  while (true) {
    gameName = _readGameName(listen);

    if (gameName.length() > 0) {
      name = gameName;
      ::close(listen);
      return true;
    }
  }
}

int init_client(string name) {
  // client logix
  /*
  create socket to listen on shout port

  as soon as we successfully read a shout
  start a client game with the name of the shout
  */

  cout << "lets find a game" << endl;
  string gameName;

  if (find_game(gameName)) {
    return client_workloop(gameName, name);
  } else {
    cout << "could not find game name" << endl;
    return 1;
  }
}

std::string exec(const char* cmd) {
  char buffer[128];
  std::string result = "";
  FILE* pipe = popen(cmd, "r");
  if (!pipe) throw std::runtime_error("popen() failed!");
  try {
      while (fgets(buffer, sizeof buffer, pipe) != NULL) {
          result += buffer;
      }
  } catch (...) {
      pclose(pipe);
      throw;
  }
  pclose(pipe);
  return result;
}

int main (int argc, char** argv) {
  cout << "will attempt to find the ipv4 bcast address" << endl;
  string bcast = exec(S_BCAST);
  cout << "found " << bcast << endl;

  if (inet_pton(AF_INET, bcast.c_str(), &util::bcast) < 1) {
    cout << "ipv4 bcast address could not be determined." << endl;
	}

  if (argc == 3) {
    if (memcmp(argv[1], _SRV, strlen(_SRV)) == 0) {
      return init_server(std::string(argv[2]));
    }
    if (memcmp(argv[1], _CLN, strlen(_CLN)) == 0) {
      return init_client(std::string(argv[2]));
    }
  }

  return _err_syntax();
}
