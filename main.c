#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>

#define PI 3.14159
#define TILE_SIZE 48
#define NAUT_RANGE (1.6*1.6)

/* sdl */

SDL_Renderer *renderer = NULL;
SDL_Window *window = NULL;

SDL_Texture *naut_tex = NULL;
SDL_Texture *spacemap_tex = NULL;
SDL_Texture *interior_tex = NULL;
SDL_Texture *font_tex = NULL;
SDL_Texture *control_tex = NULL;
SDL_Texture *ui_tex = NULL;
SDL_Texture *computer_tex = NULL;

const Uint8 *keyboard_state;

SDL_Texture *load_texture(const char *filename) {
	SDL_Surface *loaded_surface = IMG_Load(filename);
	assert(loaded_surface);
	SDL_Texture *new_texture = SDL_CreateTextureFromSurface(
			renderer, loaded_surface);
	SDL_FreeSurface(loaded_surface);
	assert(new_texture);
	return new_texture;
}

void init_sdl() {
	assert(SDL_Init(SDL_INIT_EVERYTHING) >= 0);
	window = SDL_CreateWindow("Spaceship",
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			640, 480,
			SDL_WINDOW_RESIZABLE);
	assert(window);
	renderer = SDL_CreateRenderer(window, -1,
			SDL_RENDERER_SOFTWARE);
	assert(renderer);
	SDL_SetRenderDrawColor(renderer, 0x00, 0x00, 0x00, 0xff);

	int flags = IMG_INIT_PNG;
	assert((IMG_Init(flags) & flags) == flags);

	interior_tex = load_texture("img/interior.png");
	spacemap_tex = load_texture("img/map.png");
	naut_tex = load_texture("img/astronaut.png");
	font_tex = load_texture("img/font.png");
	control_tex = load_texture("img/controls.png");
	ui_tex = load_texture("img/ui.png");
	computer_tex = load_texture("img/computer.png");

	keyboard_state = SDL_GetKeyboardState(NULL);
	SDL_ShowCursor(SDL_DISABLE);
}

void end_sdl() {
	SDL_ShowCursor(SDL_ENABLE);

	SDL_DestroyTexture(computer_tex);
	SDL_DestroyTexture(ui_tex);
	SDL_DestroyTexture(control_tex);
	SDL_DestroyTexture(font_tex);
	SDL_DestroyTexture(naut_tex);
	SDL_DestroyTexture(spacemap_tex);
	SDL_DestroyTexture(interior_tex);

	IMG_Quit();

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
}

void toggle_fullscreen() {
	int flags = SDL_GetWindowFlags(window);
	if((flags & SDL_WINDOW_SHOWN) == SDL_WINDOW_SHOWN)
		flags = SDL_WINDOW_FULLSCREEN_DESKTOP;
	else
		flags = SDL_WINDOW_SHOWN;
	SDL_SetWindowFullscreen(window, flags);
}

/* map */

struct map {
	int w, h;
	int *map;
};

struct map *load_map(const char *filename) {
	FILE *fp = fopen(filename, "r");

	struct map *map = malloc(sizeof(struct map));
	fscanf(fp, "%d%d", &map->w, &map->h);
	map->map = malloc(sizeof(int)*map->w*map->h);

	for(int i = 0; i < map->w*map->h; i++)
		fscanf(fp, "%d", &map->map[i]);

	fclose(fp);
	return map;
}

void free_map(struct map *map) {
	free(map->map);
	free(map);
}

void draw_map(struct map *map, int xo, int yo) {
	for(int x = 0; x < map->w; x++)
		for(int y = 0; y < map->h; y++) {
			int t = map->map[y*map->w+x];
			if(t < 1)
				continue;

			int orig = -1;
			if(t==4||(t>=6&&t<=16)) {
				orig = t;
				t = 1;
			}

			SDL_Rect src, dst;
draw:
			src.x = ((t-1)%4)*8;
			src.y = ((t-1)/4)*8;
			src.w = 8;
			src.h = 8;
			dst.x = x*TILE_SIZE - xo;
			dst.y = y*TILE_SIZE - yo;
			dst.w = TILE_SIZE;
			dst.h = TILE_SIZE;
			SDL_RenderCopy(renderer, interior_tex, &src, &dst);

			if(orig != -1 && t == 1) {
				t = orig;
				goto draw;
			}
		}
}

int *tile_at(struct map *map, int mx, int my) {
	for(int x = 0; x < map->w; x++)
		for(int y = 0; y < map->h; y++)
			if(mx > x*TILE_SIZE && my > y*TILE_SIZE
					&& mx < x*TILE_SIZE+TILE_SIZE
					&& my < y*TILE_SIZE+TILE_SIZE)
				return &map->map[y*map->w+x];
	return NULL;
}

/* game globals */

struct map *ship;
struct naut *player;
enum {
	STATE_INTERIOR,
	STATE_COMPUTER_ANIMATION,
	STATE_SHIP_COMPUTER,
} state;
int computer_starttime;

/* naut */

struct naut *nauts[200] = {NULL};

struct naut {
	float x, y, a, ta, xv, yv;
	float s, ts;
	struct map *map;
};

struct naut *new_naut(struct map *map, int x, int y) {
	struct naut *naut = malloc(sizeof(struct naut));
	naut->map = map;
	naut->x = x;
	naut->y = y;
	naut->a = PI;
	naut->ta = PI;
	naut->xv = 0;
	naut->yv = 0;
	naut->s = 0;
	naut->ts = 0;

	int i;
	for(i = 0; nauts[i]; i++);
	nauts[i] = naut;

	return naut;
}

void free_naut(struct naut *naut) {
	int i, j;
	for(i = 0; nauts[i+1]; i++);
	for(j = 0; nauts[j] != naut; j++);
	nauts[j] = nauts[i];
	free(naut);
}

void draw_naut(struct naut *naut, int xo, int yo) {
	SDL_RendererFlip flip = SDL_FLIP_NONE;
	if(naut->s < -0.1)
		flip = SDL_FLIP_HORIZONTAL;

	float a = (naut->a/PI)*180;

	SDL_Rect src, dst;
	if(fabs(naut->s) > 0.8)
		src.x = 16;
	else if(fabs(naut->s) > 0.5)
		src.x = 8;
	else
		src.x = 0;
	src.y = 0;
	src.w = 8;
	src.h = 8;
	dst.x = naut->x*TILE_SIZE - xo;
	dst.y = naut->y*TILE_SIZE - yo;
	dst.w = TILE_SIZE;
	dst.h = TILE_SIZE;

	SDL_Point p;
	p.x = TILE_SIZE/2;
	p.y = TILE_SIZE/2;

	SDL_RenderCopyEx(renderer, naut_tex, &src, &dst, a, &p, flip);
}

void move_naut(struct naut *naut, float x, float y) {
	float dx = naut->x+x+0.5;
	float dy = naut->y+y+0.5;
	
	if(dx < 0 || dy < 0 || dx >= naut->map->w || dy >= naut->map->h)
		return;

	int tm[] = {0,-1, 1,-1, 1,0, 1,1, 0,1, -1,1, -1,0, -1,-1};
	for(int i = 0; i < 8; i++) {
		int tx = dx + tm[i*2]*(0.2-0.1*(i%2));
		int ty = dy + tm[i*2+1]*(0.2-0.1*(i%2));

		int t = naut->map->map[ty*naut->map->w+tx];

		if(t==2||t==3||t==5||(t>=8&&t<=25)) {
			switch(i) {
			case 0:
			case 4:
				naut->yv *= -0.7;
				break;
			case 2:
			case 6:
				naut->xv *= -0.7;
				break;
			case 1:
			case 3:
			case 5:
			case 7:
				naut->xv *= -0.5;
				naut->yv *= -0.5;
				break;
			}

			return;
		}
	}

	naut->x += x;
	naut->y += y;
}

void control_naut(struct naut *naut) {
	int x = 0, y = 0;
	if(keyboard_state[SDL_SCANCODE_W]||keyboard_state[SDL_SCANCODE_UP])
		y = -1;
	if(keyboard_state[SDL_SCANCODE_S]||keyboard_state[SDL_SCANCODE_DOWN])
		y = 1;
	if(keyboard_state[SDL_SCANCODE_A]||keyboard_state[SDL_SCANCODE_LEFT])
		x = -1;
	if(keyboard_state[SDL_SCANCODE_D]||keyboard_state[SDL_SCANCODE_RIGHT])
		x = 1;
	if(x != 0 || y != 0) {
		naut->ta = atan2(y, x) + PI/2;
		if(naut->ta < 0)
			naut->ta += PI*2;
		if(naut->ta >= PI*2)
			naut->ta -= PI*2;

		if(fabs(naut->xv) < 0.1)
			naut->xv += x * 0.02;
		if(fabs(naut->yv) < 0.1)
			naut->yv += y * 0.02;
	}
}

void update_naut(struct naut *naut) {
	if(naut->xv != 0 || naut->yv != 0) {
		if(naut->ts == 0)
			naut->ts = 1;
		if(naut->s > 1)
			naut->ts = -1.0;
		if(naut->s < -1)
			naut->ts = 1.0;
	}
	else {
		naut->ts = 0;
		if(state == STATE_INTERIOR)
			naut->ta = naut->a;
	}

	if(fabs(naut->ta-naut->a) < PI && naut->ta != naut->a) {
		if(naut->a < naut->ta)
			naut->a += 0.3;
		if(naut->a > naut->ta)
			naut->a -= 0.3;
	}
	else if(naut->ta != naut->a) {
		if(naut->a < naut->ta)
			naut->a -= 0.1;
		if(naut->a > naut->ta)
			naut->a += 0.1;
	}
	if(naut->a < 0)
		naut->a += PI*2;
	if(naut->a >= PI*2)
		naut->a -= PI*2;

	if(fabs(naut->ta-naut->a) < 0.2)
		naut->a = naut->ta;
	if(fabs(naut->ta-naut->a) >= PI*2)
		naut->a = naut->ta;

	if(naut->s > naut->ts)
		naut->s -= 0.06;
	else if(naut->s < naut->ts)
		naut->s += 0.06;

	if(naut->xv != 0 || naut->yv != 0) {
		move_naut(naut, naut->xv, naut->yv);

		if(naut->xv < 0)
			naut->xv += 0.004;
		if(naut->xv > 0)
			naut->xv -= 0.004;
		if(naut->yv < 0)
			naut->yv += 0.004;
		if(naut->yv > 0)
			naut->yv -= 0.004;

		if(fabs(naut->xv) < 0.01)
			naut->xv = 0;
		if(fabs(naut->yv) < 0.01)
			naut->yv = 0;
	}
}

void update_nauts() {
	for(int i = 0; nauts[i]; i++)
		update_naut(nauts[i]);
}

void draw_nauts(int xo, int yo) {
	for(int i = 0; nauts[i]; i++)
		draw_naut(nauts[i], xo, yo);
}

/* basic ui */

void draw_text(const char *text, int x, int y, float scale) {
	SDL_Rect src, dst;
	src.w = 6;
	src.h = 8;
	dst.w = 6*scale;
	dst.h = 8*scale;
	dst.x = x;
	dst.y = y;
	for(const char *c = text; *c; c++) {
		src.x = (*c%16)*6;
		src.y = (*c/16)*8;
		SDL_RenderCopy(renderer, font_tex, &src, &dst);
		dst.x += dst.w;
		if(*c == '\n') {
			dst.x = x;
			dst.y += dst.h;
		}
	}
}

void draw_header(const char *text) {
	int w, h;
	SDL_GetWindowSize(window, &w, &h);
	int len;
	for(len = 0; text[len]; len++);

	draw_text(text, w/2-(len*4*6)/2, 2, 4);
}

/* mouse */

void draw_mouse(int xo, int yo) {
	int mx, my;
	SDL_GetMouseState(&mx, &my);

	SDL_Rect src, dst;
	src.x = 0;

	if(state == STATE_COMPUTER_ANIMATION)
		goto draw;

	if(state == STATE_SHIP_COMPUTER)
		goto draw;

	int *t = tile_at(player->map, mx+xo, my+yo);
	if(t == NULL)
		goto draw;

	int tx = (t-player->map->map) % player->map->w;
	int ty = (t-player->map->map) / player->map->w;

	char str[30];
	if(*t == 17)
		sprintf(str, "Use Computer");
	else
		goto draw;

	if(pow(tx+0.5-player->x,2)+pow(ty+0.5-player->y,2) > NAUT_RANGE) {
		SDL_SetTextureAlphaMod(ui_tex, 0x88);
		SDL_SetTextureAlphaMod(font_tex, 0x88);
	}

	draw_text(str, mx+32, my+16, 2);
	src.x = 8;

draw:
	src.y = 0;
	src.w = 8;
	src.h = 8;
	dst.x = mx - 2*4;
	dst.y = my;
	dst.w = 32;
	dst.h = 32;

	SDL_RenderCopy(renderer, ui_tex, &src, &dst);

	SDL_SetTextureAlphaMod(ui_tex, 0xff);
	SDL_SetTextureAlphaMod(font_tex, 0xff);
}

void click() {
	int w, h;
	SDL_GetWindowSize(window, &w, &h);

	int xo = player->map->w*TILE_SIZE/2 - w/2;
	int yo = player->map->h*TILE_SIZE/2 - h/2;

	int mx, my;
	SDL_GetMouseState(&mx, &my);

	int tx, ty, *t;

	switch(state) {
	case STATE_INTERIOR:
		t = tile_at(player->map, mx+xo, my+yo);
		if(t == NULL)
			break;

		tx = (t-player->map->map) % player->map->w;
		ty = (t-player->map->map) / player->map->w;

		if(*t == 17) {
			if(pow(tx+0.5-player->x,2)+pow(ty+0.5-player->y,2)
					> NAUT_RANGE)
				break;
			state = STATE_COMPUTER_ANIMATION;
			player->ta = atan2(ty-player->y, tx-player->x) + PI/2;
			computer_starttime = SDL_GetTicks();
			break;
		}

		break;

	case STATE_SHIP_COMPUTER:
		break;
	
	default:
		break;
	}
}


/* computer */

void draw_computer_animation() {
	int frame = SDL_GetTicks() - computer_starttime;
	int w, h;
	SDL_GetWindowSize(window, &w, &h);
	SDL_Rect dst;
	dst.h = h - 32 + frame*5;
	dst.w = dst.h;
	dst.x = w/2 - dst.w/2;
	dst.y = h - dst.h + frame*1.5;
	SDL_RenderCopy(renderer, computer_tex, NULL, &dst);
}

/* game */

void init_game() {
	ship = load_map("lvl/ship.lvl");
	player = new_naut(ship, 0, 0);
	for(int x = 0; x < ship->w; x++)
		for(int y = 0; y < ship->h; y++) {
			if(ship->map[y*ship->w+x] != 7)
				continue;
			player->x = x;
			player->y = y;
		}
	state = STATE_INTERIOR;
}

void end_game() {
	free_map(ship);
	free(player);
}

void draw() {
	int w, h;
	SDL_GetWindowSize(window, &w, &h);

	int xo = player->map->w*TILE_SIZE/2 - w/2;
	int yo = player->map->h*TILE_SIZE/2 - h/2;

	SDL_RenderClear(renderer);

	switch(state) {
	case STATE_INTERIOR:
		draw_map(player->map, xo, yo);
		draw_nauts(xo, yo);
		draw_header("Ship Interior");
		break;
	case STATE_COMPUTER_ANIMATION:
		draw_computer_animation();
		break;
	case STATE_SHIP_COMPUTER:
		draw_header("Galaxy");
		break;
	}

	draw_mouse(xo, yo);
	SDL_RenderPresent(renderer);
}

void update() {
	if(state == STATE_INTERIOR)
		control_naut(player);
	if(state == STATE_COMPUTER_ANIMATION)
		if(SDL_GetTicks() - computer_starttime > 500)
			state = STATE_SHIP_COMPUTER;
	update_nauts();
}

void main_loop() {
	SDL_Event event;
	int quit = 0;
	int last_update = SDL_GetTicks();
	while(!quit) {
		while(SDL_PollEvent(&event)) {
			switch(event.type) {
			case SDL_QUIT:
				quit = 1;
				break;
			case SDL_MOUSEBUTTONDOWN:
				switch(event.button.button) {
				case SDL_BUTTON_LEFT:
					click();
					break;
				}
				break;
			case SDL_KEYDOWN:
				switch(event.key.keysym.sym) {
				case SDLK_RETURN:
					if(keyboard_state[SDL_SCANCODE_LALT])
						toggle_fullscreen();
					break;
				case SDLK_ESCAPE:
					if(state == STATE_SHIP_COMPUTER)
						state = STATE_INTERIOR;
					break;
				}
				break;
			}
		}

		int redraw = 0;
		int current_time = SDL_GetTicks();
		const int delay = 20;
		while(current_time - last_update > delay) {
			last_update += delay;
			update();
			redraw = 1;
		}

		if(redraw)
			draw();
	}
}

/* main */

int main() {
	init_sdl();
	init_game();
	main_loop();
	end_game();
	end_sdl();
	return 0;
}
