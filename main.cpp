#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <deque>
#include "tumble.hpp"
//for graphics and input
#include <ncurses.h>
//for sleeping
#include <thread>
#include <chrono>

using namespace std;

void ncurses_init(int& width, int& height, bool& color, bool& mouse) {
	initscr();
	cbreak();
	noecho();
	curs_set(0); //hide cursor
	timeout(-1); //non blocking input
	nodelay(stdscr, true);
	
	//get window sizes
	getmaxyx(stdscr, height, width);
	
	color = has_colors();
	if (color) {
		start_color();
		//initialize color pairs
		for (short i = 0; i < 256; ++i) {
			init_pair(i + 1, i%16, i/16);
		}
	}
	
	keypad(stdscr, true);
	mousemask(ALL_MOUSE_EVENTS, nullptr);
	mouseinterval(0); //note: disabled BUTTON_CLICKED, ...
	mouse = has_mouse();
}

int main() {
	render_info info;
	bool hasmouse;
	//start ncurses
	ncurses_init(info.w, info.h, info.color, hasmouse);
	
	if (!hasmouse) {
		endwin();
		cout << "Mouse support needed to run this program" << endl;
		return 0;
	}
	
	
	//Tile menu
	
	//tile prefabs
	vector<tile> tiles;
	tiles.push_back(make_shared<RampTile>());
	tiles.push_back(make_shared<BitTile>());
	tiles.push_back(make_shared<GearBitTile>());
	tiles.push_back(make_shared<CrossTile>());
	tiles.push_back(make_shared<GearTile>());
	tiles.push_back(make_shared<OutputValueTile>());
	tiles.push_back(make_shared<OutputDirectionTile>());
	tiles.push_back(make_shared<ExitTile>());
	tiles.push_back(make_shared<LoopTile>());
	tiles.push_back(make_shared<RecursiveTile>());
	
	int tmenu_size = static_cast<int>(ceil(sqrt(static_cast<float>(tiles.size()))));
	Panel tmenu(0, 0,0, tmenu_size,tmenu_size);
	tmenu.SetCharacterCallback([tiles, tmenu_size](render_info& info, int x, int y) -> gfx_char {
		try {
			return tiles.at(y * tmenu_size + x)->GetGraphic(info);
		} catch (const out_of_range& e) {
			return {'\0'};
		}
	});
	tmenu.Hide();
	
	
	// Tile options
	
	Panel toptmenu(2, 0,0, 5,2);
	toptmenu.AddString(0,0, "Copy ");
	toptmenu.AddString(0,1, "Enter");
	toptmenu.Hide();
	
	
	// Greeter panel
	
	Panel welcome(-1);
	welcome.AddString(1,0, "Welcome to Turing Tumble Sim", draw_params(COLOR_YELLOW+8, true, true));
	welcome.AddString(0,1, "Controls:", draw_params(COLOR_WHITE, true));
	welcome.AddString(0,2, "WASD/arrows to move the camera");
	welcome.AddString(0,3, "F to recenter camera to (0,0)");
	welcome.AddString(0,4, "Q to quit (without saving)");
	welcome.AddString(0,5, "Enter to start simulation");
	welcome.AddString(0,6, "Backspace to go back / abort");
	welcome.AddString(0,7, "K / L to save or load the grid");
	welcome.AddString(0,8, "right click to close menus");
	welcome.AddString(0,9, "left click:", draw_params(COLOR_WHITE, true));
	welcome.AddString(0,10, "- empty tile: open tile menu");
	welcome.AddString(0,11, "- tile: interact");
	welcome.AddString(0,12, "- tile + CTRL: open options");
	
	
	//tile grid
	Grid G;
	//panel list
	Panels p;
	shared_ptr<Panel> tile_menu = make_shared<Panel>(tmenu);
	p.Add(tile_menu);
	shared_ptr<Panel> tile_opt_menu = make_shared<Panel>(toptmenu);
	p.Add(tile_opt_menu);
	p.Add(make_shared<Panel>(welcome));
	
	
	// Constants
	
	const int move_amount = 1;
	const int frames_per_tick = 5;
	
	// Variables
	
	//input variables
	MEVENT mevent; //for mouse events
	int ch; //input character or other
	string input_string;
	bool reading_string = false;
	int string_panel = -2;
	
	deque<bool> input_marbles; //used instead of vector for its pop_front()
	deque<bool> output_marbles;
	
	//input rendering callback
	auto input_marble_callback = [&input_marbles](render_info& info, int x, int y) -> gfx_char {
		if (y != 1 || x >= input_marbles.size()) return {'\0'};
		bool m = input_marbles[x];
		return {m ? '1' : '0', static_cast<short>(m ? COLOR_RED : COLOR_BLUE), COLOR_BLACK};
	};
	
	//camera
	int cx = 0, cy = 0;
	Grid* g = &G;
	vector<tuple<Grid*, int, int>> camera_stack;
	//mouse and selected tile position
	int mx, my, sx, sy;
	bool selected = false;
	bool start_input = false;
	bool copying = false;
	bool last_blink = false;
	
	//simulation variables
	float time = 0;
	int frame = 0, counter = 0;
	bool running = false, start = false, stop = false;
	
	
	//tile selection / deselection functions
	auto Select = [&selected, &sx, &sy](int x, int y) -> void {
		selected = true;
		sx = x, sy = y;
	};
	auto Deselect = [&selected, &tile_menu, &copying, &tile_opt_menu](void) -> void {
		selected = false;
		copying = false;
		tile_menu->Hide();
		tile_opt_menu->Hide();
	};
	
	auto ThrowMessage = [&p](string str, int x = 0, int y = 0) -> void {
		Panel pmsg(str, -1, x,y);
		p.Add(make_shared<Panel>(pmsg));
	};
	auto OpenStringInputBox = [&p, &input_string, &string_panel, &reading_string](int id, string str, int x = 0, int y = 0) -> void {
		input_string = "";
		Panel pinput(id, x,y, str.length(),2);
		pinput.AddString(0,0, str);
		pinput.AddString(0,1, "");
		pinput.SetRenderCallback([&input_string](Panel& pn, render_info& info, int x, int y, int w, int h) -> void {
			pn.EditString(1, input_string);
		});
		p.Add(make_shared<Panel>(pinput));
		reading_string = true;
		string_panel = id;
	};
	
	
	//game loop
	while (true) {
		//user input
		while ((ch = getch()) != ERR) {
			//for string input panels
			if (reading_string && ch != KEY_MOUSE) {
				if (ch >= 32 && ch < 128) {
					input_string += static_cast<char>(ch);
					continue;
				}
				if (ch == KEY_BACKSPACE) {
					if (!input_string.empty())
						input_string.pop_back();
					continue;
				}
				if (ch == '\n') {
					reading_string = false;
					p.RemoveAll(string_panel);
					
					ofstream save;
					ifstream load;
					switch (string_panel) {
						case 8: //save filename
							save.open(input_string + ".ttsim");
							if (!save.is_open()) {
								ThrowMessage("Could not open \"" + input_string + "\"");
								break;
							}
							g->Serialize(save);
							save.close();
							break;
						case 9: //load filename
							load.open(input_string);
							if (!load.is_open()) {
								ThrowMessage("Could not find \"" + input_string + "\"");
								break;
							}
							if (g->Deserialize(load))
								ThrowMessage("Failed to parse \"" + input_string + "\"");
							load.close();
							break;
						default:
							ThrowMessage("Internal Error: Unsure what to do with this");
							break;
					}
					continue;
				}
				continue;
			}
			
			//general controls
			switch (ch) {
				case 'q':
					endwin();
					return 0;
				case 'w':
				case KEY_UP:
					cy -= move_amount;
					sy += move_amount;
					break;
				case 's':
				case KEY_DOWN:
					cy += move_amount;
					sy -= move_amount;
					break;
				case 'a':
				case KEY_LEFT:
					cx -= move_amount * 2;
					sx += move_amount * 2;
					break;
				case 'd':
				case KEY_RIGHT:
					cx += move_amount * 2;
					sx -= move_amount * 2;
					break;
				case 'f':
					cx = 0;
					cy = 0;
					Deselect();
					break;
				//save / load
				case 'k':
					if (!start_input && !reading_string) {
						OpenStringInputBox(8, "Enter save filename");
					}
					break;
				case 'l':
					if (!start_input && !reading_string) {
						OpenStringInputBox(9, "Enter load filename");
					}
					break;
				//input marble panel
				case '0':
				case '1':
				case '\n':
					if (running) break;
					if (!start_input) {
						p.RemoveAll(-1);
						//clear input
						input_marbles.clear();
						//create input panel
						Panel pinput(1, 0,info.h-4, info.w-2,2);
						pinput.AddString(0,0, "Enter input marbles (0 / 1):");
						pinput.SetCharacterCallback(input_marble_callback);
						p.Add(make_shared<Panel>(pinput));
					}
					//add marble to list
					if (ch == '0')
						input_marbles.push_back(false);
					else if (ch == '1')
						input_marbles.push_back(true);
					else if (start_input) {
						start_input = false;
						p.RemoveAll(1);
						if (input_marbles.size() == 0) {
							ThrowMessage("Error: No marbles specified");
							break;
						}
						start = true;
						break;
					}
					start_input = true;
					break;
				case KEY_BACKSPACE:
					if (start_input) {
						if (!input_marbles.empty()) input_marbles.pop_back();
						break;
					}
					if (running) {
						stop = true;
						break;
					}
					if (!camera_stack.empty()) {
						tuple<Grid*, int, int>& b = camera_stack.back();
						g = get<0>(b);
						cx = get<1>(b);
						cy = get<2>(b);
						camera_stack.pop_back();
						break;
					}
					break;
				case KEY_MOUSE:
					if (running) break;
					//process mouse input
					if (getmouse(&mevent) == OK) {
						//get mouse position
						mx = mevent.x, my = mevent.y;
						//get click types
						bool left_click =    static_cast<bool>(mevent.bstate & BUTTON1_PRESSED);
						bool right_click =   static_cast<bool>(mevent.bstate & BUTTON3_PRESSED);
						bool control_click = static_cast<bool>(mevent.bstate & BUTTON_CTRL);
						if (!left_click && !right_click) break;
						//check if clicked on panel
						shared_ptr<Panel> pclick = nullptr;
						int ox = -1, oy = -1;
						bool inside_panel = p.Inside(mx, my, ox, oy, pclick);
						
						//get selected position in scene
						int wx, wy;
						toWorldCoords(info, cx + sx, cy + sy, wx, wy);
						tile selt = g->GetTile(wx, wy);
						
						if (inside_panel) {
							if (left_click && (ox < 0 || oy < 0)) break;
							if (right_click) {
								Deselect();
								//if not important, remove panel
								if (pclick->id < 0)
									p.Remove(pclick);
								//marble input panel
								if (pclick->id == 1) {
									p.Remove(pclick);
									start_input = false;
								}
								//string input
								if (pclick->id >= 8) {
									p.Remove(pclick);
									reading_string = false;
								}
								break;
							}
							//tile menu clicked
							if (pclick->id == 0 && selected) {
								const int off = oy * tmenu_size + ox;
								if (off >= tiles.size()) break;
								if (selt) break;
								g->AddTile(wx, wy, tiles[off]->Copy());
								Deselect();
								break;
							}
							//tile options menu clicked
							if (pclick->id == 2 && selected) {
								Deselect();
								switch (oy) {
									case 0: //Copy
										selected = true;
										copying = true;
										break;
									case 1: //Enter
										Grid *newg = selt->GetGrid();
										if (newg == nullptr) {
											ThrowMessage("Cannot enter this tile");
											break;
										}
										camera_stack.push_back({g, cx, cy});
										g = newg;
										cx = 0;
										cy = 0;
										break;
								}
								break;
							}
							break;
						}
						
						//get mouse position in scene
						toWorldCoords(info, cx + mx, cy + my, wx, wy);
						tile t = g->GetTile(wx, wy);
						
						if (left_click) {
							//interract with tile
							if (t) {
								if (!control_click) {
									t->Interract();
									Deselect();
								} else {
									//show tile options menu
									Select(mx, my);
									time = 0;
									tile_opt_menu->Show();
									tile_opt_menu->Move(mx+1, my+1);
								}
								break;
							}
							if (selected) {
								if (copying) {
									g->AddTile(wx, wy, selt->Copy());
								}
								Deselect();
								break;
							}
							//select empty tile and show menu
							Select(mx, my);
							time = 0;
							tile_menu->Show();
							tile_menu->Move(mx+1, my+1);
						} else {
							//right click
							if (!selected) {
								g->RemoveTile(wx, wy);
							}
							Deselect();
						}
					}
					break;
			}
		}
		
		
		//Rendering
		
		bool blink = (time >= 0.5 || running);
		short blink_color = (copying ? COLOR_BLUE+8 : COLOR_YELLOW+8);
		g->Render(info, cx, cy, blink, selected ? sx : -1, sy, blink_color);
		if (blink && !last_blink) {
			for (auto it = tiles.begin(); it != tiles.end(); it++)
				(*it)->Interract();
		}
		last_blink = blink;
		
		
		// Simulation
		
		if (running) {
			counter++;
			if (counter >= frames_per_tick) {
				counter = 0;
				
				bool inside = false;
				do {
					//tick scene
					frame++;
					collision_result result;
					bool add_marble = G.Update(result);
					
					if (result.output >= 0) {
						output_marbles.push_back(result.output > 0);
					}
					
					inside = result.inside_tile;
					
					if (add_marble || stop) {
						inside = false;
						if (input_marbles.size() > 0 && !stop) {
							//get next input marble
							bool m = input_marbles.front();
							input_marbles.pop_front();
							G.AddMarble(m ? 1 : -1, static_cast<short>(m ? COLOR_RED : COLOR_BLUE));
						} else {
							//simulation is done
							running = false;
							stop = false;
							G.Reset();
							
							//print output on panel
							string out_str = "Output:";
							Panel pout(-1, 0,0, max(output_marbles.size(), out_str.length()),2);
							pout.AddString(0,0, out_str);
							
							out_str = "";
							for (bool b : output_marbles)
								out_str += (b ? '1' : '0');
							pout.AddString(0,1, out_str);
							output_marbles.clear();
							
							p.Add(make_shared<Panel>(pout));
						}
					}
				} while (inside);
			}
		} else {
			p.Render(info);
			//start logic
			if (start) {
				start = false;
				running = true;
				Deselect();
				p.RemoveAll(-1);
				//ensure clean start
				G.Reset();
				output_marbles.clear();
				//get next input marble
				bool m = input_marbles.front();
				input_marbles.pop_front();
				G.AddMarble(m ? 1 : -1, static_cast<short>(m ? COLOR_RED : COLOR_BLUE));
			}
		}
		
		refresh();
		
		this_thread::sleep_for(chrono::milliseconds(49));
		time += 0.05;
		if (time > 1) time = 0;
	}
	
	//stop ncurses
	endwin();
	
	return 0;
}
