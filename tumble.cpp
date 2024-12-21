#include "tumble.hpp"

int modulo2(int x) {
	int y = x % 2;
	return y >= 0 ? y : y + 2;
}

bool isOdd(int x, int y) {
	return static_cast<bool>(modulo2(x) ^ modulo2(y));
}

void toWorldCoords(render_info& info, int x, int y, int& wx, int& wy) {
	wx = x - info.w / 2;
	wy = y - info.h / 2;
}


// Marble functions

void Marble::Start(int dir, short clr, int x, int y) {
	active = true;
	direction = dir;
	color = clr;
	this->x = x, this->y = y;
}

void Marble::Stop() {
	active = false;
}

void Marble::Update() {
	x += direction;
	y++;
}


// Grid functions

void Grid::AddTile(int x, int y, tile t) {
	tiles[{x, y}] = move(t);
}

tile Grid::GetTile(int x, int y) const {
	//get iterator of element
	auto it = tiles.find({x, y});
	//tile does not exist
	if (it == tiles.end()) return nullptr;
	
	return it->second;
}

void Grid::RemoveTile(int x, int y) {
	if (x == 0 && y == 0) return;
	tiles.erase({x, y});
}

void Grid::Interract(int x, int y) {
	tile t = GetTile(x, y);
	if (t == nullptr) return;
	
	t->Interract();
}

void Grid::AddMarble(int direction, short color) {
	marble.Start(direction, color);
}

void Grid::TurnConnected(unordered_set<pair<int, int>, IntPairHash>& v, int x, int y) {
	const int directions[4][2] = {{1,0}, {0,1}, {-1,0}, {0,-1}};
	
	for (auto& dir : directions) {
		const int i = x + dir[0], j = y + dir[1];
		
		if (v.find({i, j}) != v.end()) continue;
		
		tile t = GetTile(i, j);
		if (t == nullptr) continue;
		
		v.insert({i, j});
		if (t->Turn())
			TurnConnected(v, i, j);
	}
}

void Grid::TurnConnected(int x, int y) {
	unordered_set<pair<int, int>, IntPairHash> visited = {{x,y}};
	
	TurnConnected(visited, x, y);
}

bool Grid::Update(collision_result& result) {
	result.Reset();
	//no marble
	if (!marble.IsActive()) return true;
	
	marble.Update();
	const int x = marble.x, y = marble.y;
	tile t = GetTile(x, y);
	
	if (t == nullptr) {
		return true;
	}
	
	bool done = t->Collide(marble, result);
	if (result.turn) {
		TurnConnected(x, y);
	}
	if (done && result.marble_reset) {
		//send the event up the grid chain
		done = false;
	}
	return done;
}

void Grid::Reset() {
	marble.Stop();
	for (auto& [pos, t] : tiles)
		t->Reset();
}

void Grid::Render(render_info& info, int x, int y, bool blink, int mx, int my) const {
	//render bounds
	int start_x, start_y;
	toWorldCoords(info, x, y, start_x, start_y);
	const int end_x = start_x + info.w;
	const int end_y = start_y + info.h;
	
	for (int j = start_y; j < end_y; j++)
		for (int i = start_x; i < end_x; i++) {
			tile t = GetTile(i, j);
			
			const int x = i - start_x;
			const int y = j - start_y;
			gfx_char c = {' ', COLOR_BLACK+8, COLOR_BLACK};
			
			bool isMarble = (marble.IsActive() && i == marble.x && j == marble.y);
			
			if (t == nullptr) {
				//checkerboard pattern
				if (!isOdd(i, j)) c.c = '.';
			} else {
				//get tile's graphic
				c = t->GetGraphic(info);
			}
			
			if (isMarble && blink) {
				c = marble.GetGraphic();
			}
			
			if (x == mx && y == my && blink) {
				c.bg = COLOR_YELLOW;
			}
			
			DrawChar(c, x, y, info.color);
		}
}


// GUI

void DrawChar(gfx_char c, int x, int y, bool color) {
	if (color) {
		short pair = c.fg + c.bg*16 + 1;
		attron(COLOR_PAIR(pair));
		mvaddch(y, x, c.c);
		attroff(COLOR_PAIR(pair));
		return;
	}
	mvaddch(y, x, c.c);
}

void DrawBox(int x1, int y1, int x2, int y2) {
	//ensure correct order
	if (x1 > x2) swap(x1, x2);
	if (y1 > y2) swap(y1, y2);
	
	int dx = x2 - x1, dy = y2 - y1;
	//sides
	if (dx > 1 || dy > 1) {
		mvhline(y1, x1+1, '-', dx-1);
		mvhline(y2, x1+1, '-', dx-1);
		mvvline(y1+1, x1, '|', dy-1);
		mvvline(y1+1, x2, '|', dy-1);
	}
	
	//corners
	mvaddch(y1, x1, '+');  // Top-left corner
	mvaddch(y1, x2, '+');  // Top-right corner
	mvaddch(y2, x1, '+');  // Bottom-left corner
	mvaddch(y2, x2, '+');  // Bottom-right corner
	
	//fill inside
	for (int i = y1+1; i < y2; i++)
		mvhline(i, x1+1, ' ', dx-1);
}

void Panel::AddString(int x, int y, string s) {
	str.push_back(tuple<int, int, string>(x, y, s));
}

void Panel::Render(render_info& info) const {
	if (hide) return;
	
	//border
	DrawBox(x, y, x+w+1, y+h+1);
	
	//strings
	for (auto it = str.begin(); it != str.end(); it++) {
		mvprintw(y+1+get<1>(*it), x+1+get<0>(*it), get<2>(*it).c_str());
	}
	
	//call render function with offset and width,height
	if (renderFunc) renderFunc(info, x+1, y+1, w, h);
	
	//call character function for every pixel
	if (charFunc)
		for (int j = 0; j < h; j++)
			for (int i = 0; i < w; i++) {
				gfx_char c = charFunc(info, i, j);
				if (c.c == '\0') continue;
				DrawChar(c, i+x+1, j+y+1, info.color);
			}
}

bool Panel::Inside(int x, int y, int& ox, int& oy) const {
	int x1 = this->x + 1, y1 = this->y + 1;
	int x2 = x1 + w - 1, y2 = y1 + h - 1;
	
	
	if (x1-1 <= x && x <= x2+1 && y1-1 <= y && y <= y2+1) {
		ox = -1;
		oy = -1;
		if (x1 <= x && x <= x2 && y1 <= y && y <= y2) {
			ox = x - x1;
			oy = y - y1;
		}
		return true;
	}
	
	return false;
}

shared_ptr<Panel> Panels::Get(int id) const {
	for (auto it = panels.begin(); it != panels.end(); it++)
		if ((*it)->id == id)
			return *it;
	return nullptr;
}

void Panels::RemoveAll(int id) {
	for (auto it = panels.begin(); it != panels.end(); )
		if ((*it)->id == id) {
			it = panels.erase(it);
		} else {
			it++;
		}
}

bool Panels::Inside(int x, int y, int& ox, int& oy, shared_ptr<Panel>& p) const {
	for (auto it = panels.rbegin(); it != panels.rend(); it++) {
		if (!(*it)->IsHidden() && (*it)->Inside(x, y, ox, oy)) {
			p = *it;
			return true;
		}
	}
	return false;
}

void Panels::Render(render_info& info) const {
	for (auto it = panels.begin(); it != panels.end(); it++)
		(*it)->Render(info);
}
