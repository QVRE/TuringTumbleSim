#include <iostream>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <algorithm>
#include <ncurses.h>

using namespace std;

struct render_info {
	int w, h; //width and height of output
	bool color; //whether color is enabled
};

struct gfx_char {
	char c; //character to be rendered
	short fg, bg; //foreground and background colors
};

void DrawChar(gfx_char c, int x, int y, bool color = false);

bool isOdd(int x, int y);
void toWorldCoords(render_info& info, int x, int y, int& wx, int& wy);

//hash used for an int,int pair needed by unordered map
struct IntPairHash {
	size_t operator()(const pair<int, int>& p) const {
		uint64_t x = (static_cast<uint64_t>(p.first) << 32) | (static_cast<uint64_t>(p.second));
		return hash<uint64_t>{}(x);
	}
};


// Classes

class Marble {
private:
	int direction; // +1 / -1
	short color;
	bool active;
	
public:
	int x, y; //position
	
	Marble() : x(0), y(0), direction(-1), color(COLOR_WHITE), active(false) {}
	
	void SetColor(short clr) { color = clr; }
	int GetDirection() const { return direction; }
	void SetDirection(int d) { direction = (d >= 0 ? 1 : -1); }
	void Reflect() { direction = -direction; }
	bool IsActive() const { return active; }
	int GetValue() const {
		if (color == COLOR_BLUE) return 0;
		if (color == COLOR_RED) return 1;
		return -1;
	}
	
	void Start(int dir = 0, short clr = COLOR_BLUE, int x = 0, int y = 0);
	void Stop();
	
	void Update();
	
	gfx_char GetGraphic() const {
		return (gfx_char){'@', color, COLOR_BLACK};
	}
};


// Tile classes

class Grid; //forward declaration

struct collision_result {
	int output; //0,1 or -1 for none
	bool marble_reset; //set when marble has been reset
	bool turn; //set when tile turns neighbors
	
public:
	void Reset(void) {
		output = -1;
		marble_reset = false;
		turn = false;
	}
};

class BaseTile {
public:
	virtual shared_ptr<BaseTile> Copy() const = 0;
	
	//called when simulation starts
	virtual void Reset() {}
	//called when user clicks on tile
	virtual void Interract() {}
	//called when marble collides with tile
	virtual bool Collide(Marble& m, collision_result& result) { return false; }
	//logic for being turned by another tile
	virtual bool Turn(void) { return false; }
	
	virtual gfx_char GetGraphic(render_info& info) const {
		return (gfx_char){'?', COLOR_WHITE, COLOR_BLACK};
	}
};

typedef shared_ptr<BaseTile> tile;

class DropTile : public BaseTile {
public:
	tile Copy() const override {
		return make_shared<DropTile>(*this);
	}
	
	gfx_char GetGraphic(render_info& info) const override {
		return (gfx_char){'^', COLOR_WHITE, COLOR_BLACK};
	}
};

class OutputValueTile : public BaseTile {
public:
	tile Copy() const override {
		return make_shared<OutputValueTile>(*this);
	}
	
	bool Collide(Marble& m, collision_result& result) override {
		result.output = m.GetValue();
		return false;
	}
	
	gfx_char GetGraphic(render_info& info) const override {
		return (gfx_char){'v', COLOR_WHITE, COLOR_BLACK};
	}
};

class OutputDirectionTile : public BaseTile {
public:
	tile Copy() const override {
		return make_shared<OutputDirectionTile>(*this);
	}
	
	bool Collide(Marble& m, collision_result& result) override {
		result.output = (m.GetDirection() > 0 ? 1 : 0);
		return false;
	}
	
	gfx_char GetGraphic(render_info& info) const override {
		return (gfx_char){'w', COLOR_WHITE, COLOR_BLACK};
	}
};

class LoopTile : public BaseTile {
protected:
	short marble_color;
	
public:
	LoopTile() : marble_color(COLOR_BLUE) {}
	
	tile Copy() const override {
		return make_shared<LoopTile>(*this);
	}
	
	void Interract() override {
		if (marble_color == COLOR_BLUE)
			marble_color = COLOR_RED;
		else if (marble_color == COLOR_RED)
			marble_color = COLOR_GREEN;
		else
			marble_color = COLOR_BLUE;
	}
	
	bool Collide(Marble& m, collision_result& result) override {
		result.marble_reset = true;
		m.Start(m.GetDirection(), marble_color);
		return true;
	}
	
	gfx_char GetGraphic(render_info& info) const override {
		return (gfx_char){'Y', marble_color, COLOR_BLACK};
	}
};

class RampTile : public BaseTile {
protected:
	int direction;
	
public:
	RampTile() : direction(1) {}
	
	tile Copy() const override {
		return make_shared<RampTile>(*this);
	}
	
	void Interract() override { direction = -direction; }
	
	bool Collide(Marble& m, collision_result& result) override {
		m.SetDirection(direction);
		return false;
	}
	
	gfx_char GetGraphic(render_info& info) const override {
		const char c = (direction > 0 ? '\\' : '/');
		return (gfx_char){c, COLOR_GREEN+8, COLOR_BLACK};
	}
};

class BitTile : public RampTile {
protected:
	int current_dir;
	
public:
	BitTile() : current_dir(1) {}
	
	tile Copy() const override {
		return make_shared<BitTile>(*this);
	}
	
	void Reset() override { current_dir = direction; }
	
	void Interract() override { direction = -direction, current_dir = direction; }
	
	bool Collide(Marble& m, collision_result& result) override {
		m.SetDirection(current_dir);
		current_dir = -current_dir;
		return false;
	}
	
	gfx_char GetGraphic(render_info& info) const override {
		const char c = (current_dir > 0 ? '\\' : '/');
		return (gfx_char){c, COLOR_CYAN, COLOR_BLACK};
	}
};

class GearTile : public BaseTile {
public:
	tile Copy() const override {
		return make_shared<GearTile>(*this);
	}
	
	bool Collide(Marble& m, collision_result& result) override {
		return false;
	}
	
	bool Turn(void) override {
		return true;
	}
	
	gfx_char GetGraphic(render_info& info) const override {
		return (gfx_char){'+', COLOR_RED+8, COLOR_BLACK};
	}
};


class GearBitTile : public BitTile {
public:
	tile Copy() const override {
		return make_shared<GearBitTile>(*this);
	}
	
	bool Turn(void) override {
		current_dir = -current_dir;
		return true;
	}
	
	bool Collide(Marble& m, collision_result& result) override {
		m.SetDirection(current_dir);
		current_dir = -current_dir;
		result.turn = true;
		return false;
	}
	
	gfx_char GetGraphic(render_info& info) const override {
		const char c = (current_dir > 0 ? '\\' : '/');
		return (gfx_char){c, COLOR_MAGENTA, COLOR_BLACK};
	}
};


// Grid class

class Grid {
private:
	//sparse set of tiles
	unordered_map<pair<int, int>, tile, IntPairHash> tiles;
	Marble marble;
	
	//recursive function used by TurnConnected()
	void TurnConnected(unordered_set<pair<int, int>, IntPairHash>& v, int x, int y);
	
public:
	//tile functions
	void AddTile(int x, int y, tile t);
	tile GetTile(int x, int y) const;
	void RemoveTile(int x, int y);
	void Interract(int x, int y);
	void TurnConnected(int x, int y);
	
	//constructors
	Grid() {
		tiles = {};
		AddTile(0, 0, make_shared<DropTile>());
	}
	
	//marble functions
	void AddMarble(int direction = -1, short color = COLOR_BLUE);
	//returns true if simulation is done
	bool Update(collision_result& result);
	//called when simulation finishes, call manually to stop
	void Reset();
	
	//render function
	void Render(render_info& info, int x, int y, bool blink = true, int mx = -1, int my = -1) const;
};


// GUI

void DrawBox(int x1, int y1, int x2, int y2);

class Panel {
private:
	int x, y;
	int w, h;
	bool hide;
	
	vector<tuple<int, int, string>> str;
	
	//callback functions
	typedef function<gfx_char(render_info&, int, int)> charFunction;
	typedef function<void(render_info&, int, int, int, int)> renderFunction;
	
	//return '\0' for nothing
	charFunction charFunc;
	renderFunction renderFunc;
	
public:
	const int id;
	
	Panel(int id = -1, int x = 0, int y = 0, int w = 1, int h = 1)
		: id(id), x(x), y(y), w(w), h(h), hide(false) {}
	
	void Resize(int w, int h) { this->w = w, this->h = h; }
	void Move(int x, int y) { this->x = x, this->y = y; }
	void Hide(void) { hide = true; }
	void Show(void) { hide = false; }
	bool IsHidden(void) const { return hide; }
	
	void AddString(int x, int y, string s);
	
	void SetCharacterCallback(charFunction cf) { charFunc = cf; }
	void SetRenderCallback(renderFunction rf) { renderFunc = rf; }
	
	//if panel is touched, returns true and sets offset. Border returns an offset of (-1,-1)
	bool Inside(int x, int y, int& ox, int& oy) const;
	
	void Render(render_info& info) const;
};

class Panels {
private:
	vector<shared_ptr<Panel>> panels;
	
public:
	Panels() = default;
	
	void Add(shared_ptr<Panel> p) {
		panels.push_back(p);
	}
	void Remove(shared_ptr<Panel> p) {
		panels.erase(remove(panels.begin(), panels.end(), p), panels.end());
	}
	void RemoveAll(int id);
	
	shared_ptr<Panel> Get(int id) const;
	
	bool Inside(int x, int y, int& ox, int& oy, shared_ptr<Panel>& p) const;
	
	void Render(render_info& info) const;
};
