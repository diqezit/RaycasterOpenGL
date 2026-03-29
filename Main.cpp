#include <array>
#include <vector>
#include <algorithm>
#include <cmath>

#include "raylib.h"
#include "raymath.h"

namespace {

    // -=-=-=-=-=-=-=-= конфигурация -=-=-=-=-=-=-=-=

    constexpr int SCREEN_W = 1280;
    constexpr int SCREEN_H = 720;
    constexpr int TARGET_FPS = 60;

    constexpr int MAP_W = 32;
    constexpr int MAP_H = 32;

    constexpr int WALL_TEX_N = 3;
    constexpr int FLOOR_TEX_N = 4;

    // все текстуры спрайтов и кадров анимаций
    constexpr int SPRITE_TEX_N = 12;

    constexpr int DOOR_TEX_N = 2;

    // если поставить больше то рендер будет быстрее
    // но пиксели станут крупнее и картинка будет квадратной
    constexpr int RENDER_Q = 2;

    // небо делим на две полосы
    // сверху градиент а снизу панорама горизонта
    constexpr int SKY_DIV = 3;

    constexpr float MOVE_SPEED = 5.0f;
    constexpr float SPRINT_MULT = 2.0f;
    constexpr float MOUSE_SENS = 0.003f;
    constexpr float COLLIDE_R = 0.2f;
    constexpr float CAM_FOV = 70.0f;

    // если кадр слишком длинный то движение и двери не должны "телепортироваться"
    constexpr float MAX_DT = 0.05f;

    // near это расстояние где туман начинается
    // far это расстояние где уже ничего не видно все в тумане
    constexpr float FOG_NEAR = 5.0f;
    constexpr float FOG_FAR = 18.0f;

    // ambient это минимальный свет который есть всегда даже в темноте
    // sky это дополнительный свет для клеток под открытым небом
    constexpr float AMBIENT_LIT = 0.25f;
    constexpr float SKY_LIT = 0.55f;

    // пол рисуется из нескольких текстур и они чередуются
    // tile_size задает размер одного квадрата в мире для этого чередования
    constexpr float TILE_SIZE = 6.0f;

    // когда камера вплотную к стене расстояние может стать почти нулем
    // и тогда будет деление на ноль поэтому ставим минимум
    constexpr float MIN_WALL_DIST = 0.01f;

    constexpr float TORCH_RADIUS = 5.0f;
    constexpr float TORCH_INTENS = 0.6f;
    constexpr float MUSHROOM_SC = 0.30f;
    constexpr float TORCH_SC = 0.60f;

    // скорость открытия и закрытия двери в единицах за секунду
    // 1.6 значит дверь откроется примерно за 0.6 секунды
    constexpr float DOOR_SPEED = 1.6f;

    // насколько далеко вперед игрок дотягивается чтобы открыть дверь
    constexpr float DOOR_USE_DIST = 1.2f;

    // float не всегда доезжает ровно до 1.0
    // для коллизий и лучей разница не видна
    constexpr float DOOR_OPEN_THRESH = 0.999f;

    // стены по одной оси чуть темнее чтобы читался объем как в wolf3d
    constexpr float WALL_SIDE_SHADE = 0.78f;

    // когда луч никуда не попал ставим вот такую большую глубину
    // чтобы спрайты за этим столбцом точно не рисовались
    constexpr float MAX_DEPTH = 1e30f;

    // эти штуки предвычислены чтобы не делить каждый раз в горячих циклах
    // компилятор посчитает их на этапе компиляции
    constexpr float FOG_INV_RANGE = 1.0f / (FOG_FAR - FOG_NEAR);
    constexpr float INV_TILE = 1.0f / TILE_SIZE;
    constexpr float INV_2PI = 1.0f / (2.0f * PI);

    // минимальная величина направления луча
    constexpr float RAY_DIR_EPS = 1e-8f;

    // знаменатель расчета расстояния до пола/потолка (минимальный)
    constexpr float SURFACE_DENOM_EPS = 0.001f;

    // сколько шагов на единицу расстояния при проверке видимости факела
    // двойка значит два шага на клетку чтобы не пропустить тонкую стену
    constexpr float TORCH_RAY_DENSITY = 2.0f;

    // хеш координат для разброса фазы анимации
    // чтобы одинаковые спрайты не двигались синхронно
    constexpr int PHASE_HASH_X = 17;
    constexpr int PHASE_HASH_Y = 31;
    constexpr float PHASE_HASH_SCALE = 0.37f;

    // мерцание факела складывается из трех синусов с разными частотами
    // один sin выглядит слишком механически
    constexpr float FLICKER_BASE = 0.60f;
    constexpr float FLICKER_AMP1 = 0.25f;
    constexpr float FLICKER_FREQ1 = 1.7f;
    constexpr float FLICKER_AMP2 = 0.10f;
    constexpr float FLICKER_FREQ2 = 4.3f;
    constexpr float FLICKER_AMP3 = 0.05f;
    constexpr float FLICKER_FREQ3 = 9.2f;

    // когда туман максимальный пиксель становится вот такого цвета
    // это теплый серый
    constexpr Color FOG_CLR = { 130, 125, 118, 255 };

    // -=-=-=-=-=-=-=-= типы -=-=-=-=-=-=-=-=

    // текстура которая живет в оперативке а не на видеокарте
    // мы сами читаем из нее пиксели при рендере
    struct CpuTex {
        int w = 0;
        int h = 0;
        std::vector<Color> pixels;
    };

    // один спрайт на карте
    // pos это его позиция в мире
    // base_tex_id первый кадр анимации записывается один раз при старте
    // anim_frames сколько кадров в анимации 1 значит статичный спрайт
    // anim_fps скорость переключения кадров
    // anim_phase сдвиг фазы чтобы одинаковые спрайты не двигались синхронно
    // scale насколько он большой
    // emissive означает что спрайт сам светится и не затемняется динамическим светом
    struct SpriteObj {
        Vector2 pos = {};
        int base_tex_id = 0;
        int anim_frames = 1;
        float anim_fps = 0.0f;
        float anim_phase = 0.0f;
        float scale = 1.0f;
        bool emissive = false;
    };

    // точечный источник света например факел
    // radius на каком расстоянии свет еще действует
    // intensity насколько сильно светит прямо сейчас с учетом мерцания
    // base_intensity исходная интенсивность без мерцания
    struct PointLight {
        Vector2 pos = {};
        float radius = 0.0f;
        float base_intensity = 0.0f;
        float intensity = 0.0f;
        float flicker_amp = 0.0f;
        float flicker_speed = 0.0f;
        float flicker_phase = 0.0f;
    };

    // описание одного типа объекта на карте
    // какая текстура какой размер и светит ли он
    // anim_frames сколько кадров анимации начиная с tex_id
    // anim_fps скорость анимации в кадрах в секунду
    struct ObjDef {
        int tex_id;
        float scale;
        float light_radius;
        float light_intensity;
        float flicker_amp;
        float flicker_speed;
        bool emissive;
        int anim_frames;
        float anim_fps;
    };

    // клетка которую видит факел: координаты + предвычисленное затухание
    // стены не двигаются поэтому видимость считаем один раз при старте
    struct LitCell {
        int cell_x;
        int cell_y;
        float falloff_sq;
    };

    // дверь на карте
    // лежит в одной клетке
    // vertical true значит плоскость двери перпендикулярна оси x
    // slide_dir куда уезжает дверь при открытии +1 или -1
    // open текущее состояние 0 закрыта 1 открыта
    // target куда едем 0 или 1
    struct DoorObj {
        int cell_x = 0;
        int cell_y = 0;
        int tex_id = 0;
        bool vertical = true;
        int slide_dir = 1;
        float open = 0.0f;
        float target = 0.0f;
    };

    // игрок
    // pos где стоит
    // dir куда смотрит единичный вектор
    // plane плоскость камеры перпендикулярна dir и задает ширину обзора
    // нулевые значения чтобы msvc не ругался на неинициализированные поля
    struct Player {
        Vector2 pos = {};
        Vector2 dir = {};
        Vector2 plane = {};
    };

    // все данные одного уровня
    // четыре 2d массива стены потолки объекты двери
    // плюс точка спавна и угол поворота
    struct LevelData {
        const int (*walls)[MAP_W];
        const int (*ceilings)[MAP_W];
        const int (*objects)[MAP_W];
        const int (*doors)[MAP_W];
        Vector2 spawn;
        float spawn_angle;
    };

    // все текстуры игры в одном месте
    struct Assets {
        std::array<CpuTex, WALL_TEX_N> walls;
        std::array<CpuTex, FLOOR_TEX_N> floors;
        std::array<CpuTex, SPRITE_TEX_N> sprites;
        std::array<CpuTex, DOOR_TEX_N> doors;
        CpuTex ceiling;
        CpuTex sky_top;
        CpuTex sky_side;
    };

    // -=-=-=-=-=-=-=-= общие хелперы -=-=-=-=-=-=-=-=

    // проверка что координаты клетки внутри карты
    // явные сравнения вместо unsigned cast чтобы MSVC анализатор
    // видел что после проверки координаты гарантированно >= 0
    bool out_of_map(int cell_x, int cell_y) {
        return cell_x < 0 or cell_x >= MAP_W
            or cell_y < 0 or cell_y >= MAP_H;
    }

    // -=-=-=-=-=-=-=-= освещение -=-=-=-=-=-=-=-=

    // все данные освещения собраны в одну структуру
    // чтобы невозможно было забыть вызвать одну из функций
    // или вызвать их не в том порядке
    //
    // static_map считается один раз при старте уровня
    // и хранит базовый свет от неба и ambient для каждой клетки
    //
    // dynamic_map пересчитывается каждый кадр
    // и хранит свет от факелов с учетом мерцания
    //
    // torch_reach предвычисляет какие клетки видит каждый факел
    // стены не двигаются поэтому видимость считаем один раз при старте
    // двери специально не учитываем чтобы не пересчитывать каждый кадр
    //
    // две точки входа
    // rebuild_all при старте уровня static - reach - dynamic
    // update_frame каждый кадр intensities - dynamic
    //
    struct Lighting {
        float static_map[MAP_H][MAP_W] = {};
        float dynamic_map[MAP_H][MAP_W] = {};

        // для каждого факела хранит список клеток которые он может осветить
        // вместе с предвычисленным затуханием (falloff_sq)
        std::vector<std::vector<LitCell>> torch_reach;

        // вызывается один раз при старте уровня
        // порядок важен тк сначала статика потом видимость потом динамика
        void rebuild_all(const LevelData& level, std::vector<PointLight>& lights, float time_sec) {
            build_static(level);
            build_reach(level, lights);
            update_intensities(lights, time_sec);
            build_dynamic(lights);
        }

        // вызывается каждый кадр
        // сначала обновляем интенсивности факелов с учетом мерцания
        // потом пересчитываем динамическую карту света
        void update_frame(std::vector<PointLight>& lights, float time_sec) {
            update_intensities(lights, time_sec);
            build_dynamic(lights);
        }

        // берем свет для точки в мире
        // просто смотрим в какую клетку попадает точка
        // и складываем статический и динамический свет
        // clamp нужен чтобы не выйти за пределы массива
        float sample(float world_x, float world_y) const {
            int cell_x = std::clamp((int)world_x, 0, MAP_W - 1);
            int cell_y = std::clamp((int)world_y, 0, MAP_H - 1);
            return std::min(static_map[cell_y][cell_x] + dynamic_map[cell_y][cell_x], 1.0f);
        }

    private:

        // несколько наложенных синусов дают неравномерное дрожание
        // один sin выглядит слишком механически
        // flicker_amp контролирует амплитуду дрожания для конкретного факела
        static float calc_flicker(const PointLight& torch, float time_sec) {
            float t = time_sec * torch.flicker_speed + torch.flicker_phase;

            float v = FLICKER_BASE
                + FLICKER_AMP1 * std::sin(t * FLICKER_FREQ1)
                + FLICKER_AMP2 * std::sin(t * FLICKER_FREQ2)
                + FLICKER_AMP3 * std::sin(t * FLICKER_FREQ3);

            v = std::clamp(v, 0.0f, 1.0f);
            return 1.0f - torch.flicker_amp * v;
        }

        // пересчитываем текущую интенсивность каждого факела
        // base_intensity это исходная яркость без мерцания
        // intensity это то что реально используется при расчете света
        static void update_intensities(std::vector<PointLight>& lights, float time_sec) {
            for (PointLight& torch : lights) {
                torch.intensity = torch.base_intensity * calc_flicker(torch, time_sec);
            }
        }

        // строим статическую карту освещения от неба
        // каждая клетка получает как минимум AMBIENT_LIT
        // если клетка без потолка или стена граничит с открытым небом
        // то добавляем SKY_LIT
        // результат не меняется после старта уровня
        void build_static(const LevelData& level) {
            constexpr int OFFS[4][2] = {
                { -1,  0 }, { 1,  0 }, { 0, -1 }, { 0,  1 }
            };

            for (int cell_y = 0; cell_y < MAP_H; ++cell_y) {
                for (int cell_x = 0; cell_x < MAP_W; ++cell_x) {
                    float lit = AMBIENT_LIT;

                    if (level.walls[cell_y][cell_x] != 0) {
                        // для стен проверяем соседей
                        // если рядом есть открытая клетка без потолка
                        // значит эта сторона стены снаружи и получает свет неба
                        bool has_open = false;

                        for (int i = 0; i < 4; ++i) {
                            int ncell_x = cell_x + OFFS[i][0];
                            int ncell_y = cell_y + OFFS[i][1];

                            if (out_of_map(ncell_x, ncell_y)) continue;

#pragma warning(suppress:6385) // координаты уже проверены выше
                            if (level.walls[ncell_y][ncell_x] == 0 and level.ceilings[ncell_y][ncell_x] == 0) {
                                has_open = true;
                                break;
                            }
                        }

                        if (has_open) lit += SKY_LIT;
                    }
                    else {
                        // пустая клетка без потолка получает свет неба напрямую
                        if (level.ceilings[cell_y][cell_x] == 0) lit += SKY_LIT;
                    }

                    static_map[cell_y][cell_x] = std::min(lit, 1.0f);
                }
            }
        }

        // для каждого факела предвычисляем список клеток которые он может осветить
        // шагаем мелкими шагами от факела к центру каждой клетки в радиусе
        // если на пути встретилась стена то свет сюда не проходит
        // TORCH_RAY_DENSITY шагов на единицу расстояния чтобы не пропустить тонкую стену
        //
        // для каждой видимой клетки сохраняем квадрат затухания
        // чем ближе к факелу тем ярче
        // квадрат от квадрата дает плавное затухание
        void build_reach(const LevelData& level, const std::vector<PointLight>& lights) {
            torch_reach.clear();
            if (lights.empty()) return;

            torch_reach.resize(lights.size());

            int light_count = (int)lights.size();
            for (int i = 0; i < light_count; ++i) {
                const PointLight& torch = lights[i];
                if (torch.radius <= 0.0f or torch.base_intensity <= 0.0f) continue;

                float radius_sq = torch.radius * torch.radius;

                // ограничиваем область проверки квадратом вокруг факела
                int min_cell_x = std::max(0, (int)(torch.pos.x - torch.radius));
                int max_cell_x = std::min(MAP_W - 1, (int)(torch.pos.x + torch.radius));
                int min_cell_y = std::max(0, (int)(torch.pos.y - torch.radius));
                int max_cell_y = std::min(MAP_H - 1, (int)(torch.pos.y + torch.radius));

                for (int cell_y = min_cell_y; cell_y <= max_cell_y; ++cell_y) {
                    for (int cell_x = min_cell_x; cell_x <= max_cell_x; ++cell_x) {
                        // проверяем расстояние до центра клетки
                        float world_x = cell_x + 0.5f;
                        float world_y = cell_y + 0.5f;

                        float delta_x = world_x - torch.pos.x;
                        float delta_y = world_y - torch.pos.y;
                        float dist_sq = delta_x * delta_x + delta_y * delta_y;

                        // клетка за пределами радиуса факела
                        if (dist_sq >= radius_sq) continue;

                        // идем от факела к клетке маленькими шагами
                        // если на пути встретилась стена то свет сюда не проходит
                        int ray_steps = std::max((int)(std::sqrt(dist_sq) * TORCH_RAY_DENSITY), 1);

                        float ray_step_x = delta_x / ray_steps;
                        float ray_step_y = delta_y / ray_steps;
                        float ray_pos_x = torch.pos.x;
                        float ray_pos_y = torch.pos.y;
                        bool blocked = false;

                        for (int j = 0; j < ray_steps; ++j) {
                            ray_pos_x += ray_step_x;
                            ray_pos_y += ray_step_y;

                            int check_cell_x = (int)ray_pos_x;
                            int check_cell_y = (int)ray_pos_y;

                            // если вдруг вышли за карту то считаем что свет не дошел
                            if (out_of_map(check_cell_x, check_cell_y)) {
                                blocked = true;
                                break;
                            }

                            // дошли до нужной клетки раньше чем уперлись в стену
                            if (check_cell_x == cell_x and check_cell_y == cell_y) break;

                            // стена на пути значит клетка в тени
                            if (level.walls[check_cell_y][check_cell_x] != 0) {
                                blocked = true;
                                break;
                            }
                        }

                        if (blocked) continue;

                        // сохраняем клетку с предвычисленным затуханием
                        float falloff = 1.0f - dist_sq / radius_sq;
                        torch_reach[i].push_back({ cell_x, cell_y, falloff * falloff });
                    }
                }
            }
        }

        // каждый кадр просто умножаем предвычисленные falloff на текущую intensity
        // никаких raycast тут нет вся дорогая работа уже сделана в build_reach
        // ограничиваем свет сразу при накоплении
        // так не нужен второй проход по всей карте
        void build_dynamic(const std::vector<PointLight>& lights) {
            // быстро обнуляем всю карту
            std::fill(&dynamic_map[0][0], &dynamic_map[0][0] + MAP_H * MAP_W, 0.0f);

            if (lights.empty()) return;

            // защита от рассинхрона (если вдруг забыли пересобрать torch_reach)
            if (torch_reach.size() != lights.size()) return;

            int light_count = (int)lights.size();
            for (int i = 0; i < light_count; ++i) {
                const PointLight& torch = lights[i];
                const std::vector<LitCell>& cells = torch_reach[i];

                if (torch.intensity <= 0.0f or cells.empty()) continue;

                for (const LitCell& cell : cells) {
                    float& lit = dynamic_map[cell.cell_y][cell.cell_x];
                    lit = std::min(lit + torch.intensity * cell.falloff_sq, 1.0f);
                }
            }
        }
    };

    // состояние игры
    // игрок плюс списки спрайтов источников света и дверей
    struct GameState {
        Player player;
        std::vector<SpriteObj> sprites;
        std::vector<PointLight> lights;
        std::vector<DoorObj> doors_list;
        Lighting lighting;

        // быстрый доступ к двери по координатам клетки
        // -1 значит двери нет
        // заполняется в init_game перед использованием
        int door_index_map[MAP_H][MAP_W] = {};
    };

    // предвычисленные данные строки пола/потолка
    // row_dist < 0 значит строка невалидна
    struct SurfaceRow {
        float row_dist = -1.0f;
        float step_x = 0.0f;
        float step_y = 0.0f;
        float world_x = 0.0f;
        float world_y = 0.0f;
    };

    // все что нужно рендеру на один кадр
    // собираем в одну структуру чтобы не передавать кучу параметров в каждую функцию
    // width_f и height_f это float версии ширины и высоты
    // чтобы не кастовать каждый раз в каждой рендер функции
    // player_pos, view_dir, view_plane распакованы из player
    // чтобы не распаковывать в каждой рендер функции отдельно
    // base_step и ray_left предвычислены один раз для ceiling и floor
    struct RenderCtx {
        const GameState* game;
        const Assets* assets;
        const LevelData* level;
        Color* framebuf;
        float* depth_buf;
        int width;
        int height;
        int half_h;
        int quality;
        float time_sec;
        float width_f;
        float height_f;
        Vector2 player_pos;
        Vector2 view_dir;
        Vector2 view_plane;
        float base_step_x;
        float base_step_y;
        float ray_left_x;
        float ray_left_y;
    };

    // -=-=-=-=-=-=-=-= описания объектов -=-=-=-=-=-=-=-=

    // таблица всех типов объектов
    // по id из карты берем отсюда текстуру размер и параметры света
    // id 0 пустой поэтому реальные объекты начинаются с 1
    // anim_frames = 1 значит статичный спрайт без анимации
    // anim_frames > 1 значит кадры идут подряд начиная с tex_id
    constexpr std::array<ObjDef, 8> OBJ_DEFS = { {
            //  tex  scale         l_rad         l_int         fl_amp fl_spd emis   frames fps
            {   0,   0.0f,         0.0f,         0.0f,         0.0f,  0.0f,  false, 1,     0.0f  },
            {   0,   MUSHROOM_SC,  0.0f,         0.0f,         0.0f,  0.0f,  false, 1,     0.0f  },
            {   1,   MUSHROOM_SC,  0.0f,         0.0f,         0.0f,  0.0f,  false, 1,     0.0f  },
            {   2,   1.0f,         0.0f,         0.0f,         0.0f,  0.0f,  false, 1,     0.0f  },
            {   3,   1.0f,         0.0f,         0.0f,         0.0f,  0.0f,  false, 1,     0.0f  },
            {   4,   TORCH_SC,     TORCH_RADIUS, TORCH_INTENS, 0.30f, 6.0f,  true,  1,     0.0f  },
            {   5,   TORCH_SC,     TORCH_RADIUS, TORCH_INTENS, 0.24f, 5.3f,  true,  1,     0.0f  },
            // bat1: 6 кадров подряд начиная с tex_id = 6
            {   6,   0.65f,        0.0f,         0.0f,         0.0f,  0.0f,  false, 6,     10.0f }
        } };

    // -=-=-=-=-=-=-=-= пути к текстурам -=-=-=-=-=-=-=-=

    constexpr std::array<const char*, WALL_TEX_N> WALL_PATHS = {
        "texture/wall1.png", "texture/wall2.png", "texture/wall3.png"
    };

    constexpr std::array<const char*, FLOOR_TEX_N> FLOOR_PATHS = {
        "texture/floor1.png", "texture/floor2.png",
        "texture/floor3.png", "texture/floor4.png"
    };

    constexpr std::array<const char*, SPRITE_TEX_N> SPRITE_PATHS = {
        "texture/grib1.png", "texture/grib2.png",
        "texture/tree1.png", "texture/tree2.png",
        "texture/torch1.png", "texture/torch2.png",

        "texture/creatures/bat1_fr1.png",
        "texture/creatures/bat1_fr2.png",
        "texture/creatures/bat1_fr3.png",
        "texture/creatures/bat1_fr4.png",
        "texture/creatures/bat1_fr5.png",
        "texture/creatures/bat1_fr6.png"
    };

    constexpr std::array<const char*, DOOR_TEX_N> DOOR_PATHS = {
        "texture/door1.png", "texture/door2.png"
    };

    // -=-=-=-=-=-=-=-= данные уровня -=-=-=-=-=-=-=-=

    // карта стен
    // 0 значит пусто а числа 1 2 3 выбирают какая текстура стены
    // по краям карты стоят единицы это внешние стены
    // внутри четыре комнаты по углам коридоры между ними и одна комната в центре
    constexpr int LVL1_WALLS[MAP_H][MAP_W] = {
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,2,2,2,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,3,3,3,3,0,0,1},
        {1,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,3,0,0,1},
        {1,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,3,0,0,1},
        {1,0,0,2,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,3,0,0,1},
        {1,0,0,2,2,0,2,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,3,0,3,3,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,3,3,3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,0,0,1},
        {1,0,0,3,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,2,0,0,1},
        {1,0,0,3,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,2,0,0,1},
        {1,0,0,3,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,2,0,0,1},
        {1,0,0,3,3,0,3,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,0,2,2,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
        {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
    };

    // карта потолков
    // 0 значит потолка нет и там видно небо
    // 1 значит есть потолок и рисуем текстуру потолка
    // потолки есть только в комнатах а коридоры под открытым небом
    constexpr int LVL1_CEIL[MAP_H][MAP_W] = {
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0},
        {0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0},
        {0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0},
        {0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0},
        {0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0},
        {0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0},
        {0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0},
        {0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0},
        {0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
    };

    // карта объектов
    // числа это id из таблицы obj_defs
    // 0 значит тут ничего нет
    // факелы грибы деревья разбросаны по карте
    constexpr int LVL1_OBJS[MAP_H][MAP_W] = {
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,5,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,0,0,0,6,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,5,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,5,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,7,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,5,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,6,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,6,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,4,0,0,0,0,0,0,0,0,0,5,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,2,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
    };

    // карта дверей
    // 0 нет двери
    // 1 дверь с текстурой door1.png
    // 2 дверь с текстурой door2.png
    // дверь ставится в пустую клетку (там где walls == 0)
    // дверь должна быть зажата между двумя стенами слева/справа или сверху/снизу
    // иначе не понятно куда ей уезжать при открытии
    constexpr int LVL1_DOORS[MAP_H][MAP_W] = {
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
    };

    constexpr LevelData LEVEL_1 = {
        LVL1_WALLS, LVL1_CEIL, LVL1_OBJS, LVL1_DOORS, { 16.0f, 16.0f }, 90.0f
    };

    // -=-=-=-=-=-=-=-= туман и затемнение -=-=-=-=-=-=-=-=

    // переводим расстояние в число от 0 до 1
    // 0 значит тумана нет вообще
    // 1 значит полный туман ничего не видно
    // между near и far туман плавно нарастает
    float calc_fog(float dist) {
        return std::clamp((dist - FOG_NEAR) * FOG_INV_RANGE, 0.0f, 1.0f);
    }

    // берем цвет пикселя и делаем с ним две вещи
    // сначала домножаем на свет чтобы затемнить
    // потом смешиваем с цветом тумана чем дальше тем больше тумана
    Color apply_shade(Color src, float light, float fog) {
        float r = src.r * light;
        float g = src.g * light;
        float b = src.b * light;

        return {
            (unsigned char)(r + (FOG_CLR.r - r) * fog),
            (unsigned char)(g + (FOG_CLR.g - g) * fog),
            (unsigned char)(b + (FOG_CLR.b - b) * fog),
            src.a
        };
    }

    // -=-=-=-=-=-=-=-= работа с текстурами -=-=-=-=-=-=-=-=

    // берем дробную часть координаты и переводим в индекс пикселя текстуры
    // это нужно чтобы текстура бесконечно повторялась
    // для отрицательных координат сдвигаем дробную часть в плюс
    int wrap_coord(float val, int size) {
        int ival = (int)val;
        float frac = val - (float)ival;
        if (frac < 0.0f) frac += 1.0f;

        // иногда из за точности float может получиться ровно size
        // min не дает выйти за границу массива
        return std::min((int)(frac * size), size - 1);
    }

    // читаем пиксель из cpu текстуры по координатам u v
    Color sample_tex(const CpuTex& tex, float u, float v) {
        return tex.pixels[wrap_coord(v, tex.h) * tex.w + wrap_coord(u, tex.w)];
    }

    // грузим картинку с диска в обычный массив пикселей
    // если файл не нашелся возвращаем яркий розовый пиксель 1x1
    // так сразу видно что текстура не загрузилась
    CpuTex load_texture(const char* path) {
        Image img = LoadImage(path);
        if (!img.data) return { 1, 1, std::vector<Color>{ MAGENTA } };

        Color* colors = LoadImageColors(img);
        CpuTex tex{ img.width, img.height,
            std::vector<Color>(colors, colors + img.width * img.height) };

        UnloadImageColors(colors);
        UnloadImage(img);
        return tex;
    }

    // грузим массив текстур по массиву путей
    template <std::size_t N>
    void load_tex_set(std::array<CpuTex, N>& out, const std::array<const char*, N>& paths) {
        for (int i = 0; i < (int)N; ++i) {
            out[i] = load_texture(paths[i]);
        }
    }

    // грузим вообще все текстуры которые нужны игре
    void load_all_assets(Assets& assets) {
        load_tex_set(assets.walls, WALL_PATHS);
        load_tex_set(assets.floors, FLOOR_PATHS);
        load_tex_set(assets.sprites, SPRITE_PATHS);
        load_tex_set(assets.doors, DOOR_PATHS);
        assets.ceiling = load_texture("texture/ceiling.png");
        assets.sky_top = load_texture("texture/sky_top1.png");
        assets.sky_side = load_texture("texture/sky_side1.png");
    }

    // -=-=-=-=-=-=-=-= вывод в cpu буфер -=-=-=-=-=-=-=-=

    // рисуем блок пикселей размером quality x quality
    // если quality равен 1 то просто один пиксель
    // если больше то заполняем квадрат одним цветом
    // это и есть суть понижения разрешения для скорости
    //
    // color.a = 255 на случай если текстура содержит полупрозрачные пиксели
    // которые прошли через alpha-skip (a != 0 но и не 255)
    // в cpu framebuffer альфа не нужна и может сломать UpdateTexture
    void put_block(const RenderCtx& rc, int scr_x, int scr_y, Color color) {
        color.a = 255;

        if (rc.quality <= 1) {
            rc.framebuf[scr_y * rc.width + scr_x] = color;
            return;
        }

        int end_x = std::min(scr_x + rc.quality, rc.width);
        int end_y = std::min(scr_y + rc.quality, rc.height);

        for (int py = scr_y; py < end_y; ++py) {
            for (int px = scr_x; px < end_x; ++px) {
                rc.framebuf[py * rc.width + px] = color;
            }
        }
    }

    // -=-=-=-=-=-=-=-= двери helpers -=-=-=-=-=-=-=-=

    // указатель двери в клетке или nullptr если двери нет
    template <typename Game>
    auto find_door(Game& game, int cell_x, int cell_y)
        -> decltype(&game.doors_list[0])
    {
        int idx = game.door_index_map[cell_y][cell_x];
        if (idx < 0) return nullptr;
        return &game.doors_list[idx];
    }

    // дверь это одна плоскость по центру клетки а не тонкий куб
    // так она не конфликтует с dda который шагает по целым клеткам
    //
    // открытие работает через сдвиг координаты попадания вдоль двери
    // сама плоскость остается в той же клетке но луч проходит
    // через освободившуюся часть проема
    //
    // обе оси обрабатываем одним кодом через массивы
    // для vertical двери главная ось x и луч пересекает её по rdx
    // для horizontal двери главная ось y и луч пересекает её по rdy
    //
    // возвращает сырой u без зеркалирования
    // зеркалирование делается в рендере рядом с draw_column
    //
    bool ray_hit_door(
        const DoorObj& door,
        float rpx, float rpy,
        float rdx, float rdy,
        float& out_dist, float& out_u, int& out_side) {

        if (door.open >= DOOR_OPEN_THRESH) return false;

        float pos[2] = { rpx, rpy };
        float dir[2] = { rdx, rdy };
        int cell[2] = { door.cell_x, door.cell_y };

        int main_ax = 1;
        if (door.vertical) main_ax = 0;
        int cross_ax = 1 - main_ax;

        out_side = main_ax;

        if (std::abs(dir[main_ax]) < RAY_DIR_EPS) return false;

        float t = ((float)cell[main_ax] + 0.5f - pos[main_ax]) / dir[main_ax];
        if (t <= MIN_WALL_DIST) return false;

        float u = pos[cross_ax] + t * dir[cross_ax] - (float)cell[cross_ax];
        if (u < 0.0f or u > 1.0f) return false;

        u -= door.open * (float)door.slide_dir;
        if (u < 0.0f or u > 1.0f) return false;

        out_dist = t;
        out_u = u;
        return true;
    }

    // двери плавно едут к target каждый кадр
    // без этого они бы телепортировались между открыто и закрыто
    void update_doors(GameState& game, float dt) {
        for (DoorObj& door : game.doors_list) {
            if (door.open < door.target) {
                door.open = std::min(door.open + DOOR_SPEED * dt, door.target);
            }
            else if (door.open > door.target) {
                door.open = std::max(door.open - DOOR_SPEED * dt, door.target);
            }
        }
    }

    // проверяем клетку перед игроком на расстоянии вытянутой руки
    // если там дверь то переключаем ей цель между открыто и закрыто
    void use_door(GameState& game) {
        float world_x = game.player.pos.x + game.player.dir.x * DOOR_USE_DIST;
        float world_y = game.player.pos.y + game.player.dir.y * DOOR_USE_DIST;

        int cell_x = (int)world_x;
        int cell_y = (int)world_y;

        if (out_of_map(cell_x, cell_y)) return;

        DoorObj* door = find_door(game, cell_x, cell_y);
        if (!door) return;

        if (door->target < 0.5f) {
            door->target = 1.0f;
        }
        else {
            door->target = 0.0f;
        }
    }

    // -=-=-=-=-=-=-=-= движение и логика -=-=-=-=-=-=-=-=

    // проверяем можно ли игроку стоять в точке x y
    // смотрим все четыре угла его "квадратика" столкновений
    // если хоть один угол попадает в стену значит нельзя
    // закрытая дверь тоже считается препятствием
    bool is_passable(const LevelData& level, const GameState& game, float world_x, float world_y) {
        int left_cell_x = std::clamp((int)(world_x - COLLIDE_R), 0, MAP_W - 1);
        int right_cell_x = std::clamp((int)(world_x + COLLIDE_R), 0, MAP_W - 1);
        int top_cell_y = std::clamp((int)(world_y - COLLIDE_R), 0, MAP_H - 1);
        int bottom_cell_y = std::clamp((int)(world_y + COLLIDE_R), 0, MAP_H - 1);

        int cx[2] = { left_cell_x, right_cell_x };
        int cy[2] = { top_cell_y, bottom_cell_y };

        for (int iy = 0; iy < 2; ++iy) {
            for (int ix = 0; ix < 2; ++ix) {
                int cell_x = cx[ix];
                int cell_y = cy[iy];

                if (level.walls[cell_y][cell_x]) return false;

                // закрытая дверь блокирует проход так же как стена
                const DoorObj* door = find_door(game, cell_x, cell_y);
                if (door and door->open < DOOR_OPEN_THRESH) return false;
            }
        }

        return true;
    }

    // пробуем сдвинуть игрока на dx dy
    // проверяем x и y отдельно друг от друга
    // благодаря этому игрок скользит вдоль стены а не застревает
    void try_move(Player& player, const LevelData& level, const GameState& game,
        float move_x, float move_y) {
        if (is_passable(level, game, player.pos.x + move_x, player.pos.y)) player.pos.x += move_x;
        if (is_passable(level, game, player.pos.x, player.pos.y + move_y)) player.pos.y += move_y;
    }

    // инициализация всей игры
    // ставим игрока на спавн и настраиваем камеру
    // проходим по карте объектов и создаем спрайты и источники света
    // в конце строим карту освещения
    void init_game(GameState& game, const LevelData& level) {
        float angle = level.spawn_angle * DEG2RAD;

        // длина плоскости камеры определяет ширину обзора fov
        // чем длиннее плоскость тем шире видно
        float plane_len = std::tan(CAM_FOV * DEG2RAD * 0.5f);
        float cos_a = std::cos(angle);
        float sin_a = std::sin(angle);

        game.player.pos = level.spawn;
        game.player.dir = { cos_a, sin_a };
        game.player.plane = { -sin_a * plane_len, cos_a * plane_len };

        game.sprites.clear();
        game.lights.clear();
        game.doors_list.clear();
        std::fill(&game.door_index_map[0][0], &game.door_index_map[0][0] + MAP_H * MAP_W, -1);

        // проходим по каждой клетке карты и ищем объекты и двери
        for (int cell_y = 0; cell_y < MAP_H; ++cell_y) {
            for (int cell_x = 0; cell_x < MAP_W; ++cell_x) {

                // -=-=-=-=-=-=-=-= объекты -=-=-=-=-=-=-=-=

                int obj_id = level.objects[cell_y][cell_x];

                // 0 значит пусто а слишком большой id значит ошибка в данных
                if (obj_id > 0 and obj_id < (int)OBJ_DEFS.size()) {
                    const ObjDef& def = OBJ_DEFS[obj_id];

                    // ставим объект в центр клетки
                    Vector2 obj_pos = { cell_x + 0.5f, cell_y + 0.5f };

                    // фаза анимации из координат чтобы одинаковые спрайты не двигались синхронно
                    float phase = (float)(cell_x * PHASE_HASH_X + cell_y * PHASE_HASH_Y) * PHASE_HASH_SCALE;

                    SpriteObj sprite;
                    sprite.pos = obj_pos;
                    sprite.base_tex_id = def.tex_id;
                    sprite.anim_frames = def.anim_frames;
                    sprite.anim_fps = def.anim_fps;
                    sprite.anim_phase = phase;
                    sprite.scale = def.scale;
                    sprite.emissive = def.emissive;
                    game.sprites.push_back(sprite);

                    // если у объекта ненулевой радиус света значит он светит как факел
                    if (def.light_radius > 0.0f) {
                        PointLight light;
                        light.pos = obj_pos;
                        light.radius = def.light_radius;
                        light.base_intensity = def.light_intensity;
                        light.flicker_amp = def.flicker_amp;
                        light.flicker_speed = def.flicker_speed;
                        light.flicker_phase = phase;
                        game.lights.push_back(light);
                    }
                }

                // -=-=-=-=-=-=-=-= двери -=-=-=-=-=-=-=-=

                int door_id = level.doors[cell_y][cell_x];
                if (door_id <= 0) continue;

                DoorObj door;
                door.cell_x = cell_x;
                door.cell_y = cell_y;
                door.tex_id = std::clamp(door_id - 1, 0, DOOR_TEX_N - 1);

                bool left_wall = (cell_x > 0) and (level.walls[cell_y][cell_x - 1] != 0);
                bool right_wall = (cell_x < MAP_W - 1) and (level.walls[cell_y][cell_x + 1] != 0);
                bool up_wall = (cell_y > 0) and (level.walls[cell_y - 1][cell_x] != 0);
                bool down_wall = (cell_y < MAP_H - 1) and (level.walls[cell_y + 1][cell_x] != 0);

                // дверь ставится в пустую клетку и должна быть зажата между двумя стенами
                // иначе не понятно как она ориентирована и куда ей уезжать при открытии
                if (left_wall and right_wall) {
                    door.vertical = false;
                }

                // выбираем куда уезжать так чтобы прятаться в стену а не в проход
                // обе оси обрабатываем одним кодом через массивы
                // neg = сосед со стороны уменьшения координаты
                // pos = сосед со стороны увеличения координаты
                bool neg_wall[2] = { left_wall, up_wall };
                bool pos_wall[2] = { right_wall, down_wall };

                int main_ax = 1;
                if (door.vertical) main_ax = 0;
                int cross_ax = 1 - main_ax;

                // slide_dir по умолчанию 1, меняем только если нужно прятаться в neg стену
                if (neg_wall[cross_ax] and !pos_wall[cross_ax]) door.slide_dir = -1;

                int door_index = (int)game.doors_list.size();
                game.doors_list.push_back(door);
                game.door_index_map[cell_y][cell_x] = door_index;
            }
        }

        // теперь когда знаем все источники света можно посчитать lightmap
        // rebuild_all гарантирует правильный порядок
        // static - reach - dynamic
        game.lighting.rebuild_all(level, game.lights, 0.0f);
    }

    // каждый кадр читаем ввод и обновляем позицию и поворот игрока
    void update_game(GameState& game, const LevelData& level, float dt, float time_sec) {
        Player& player = game.player;

        if (IsKeyPressed(KEY_E)) use_door(game);
        update_doors(game, dt);

        float speed = MOVE_SPEED * dt;
        if (IsKeyDown(KEY_LEFT_SHIFT)) speed *= SPRINT_MULT;

        // боковое движение считаем через перпендикуляр к направлению
        // чтобы скорость стрейфа не зависела от fov
        Vector2 side_dir = { -player.dir.y, player.dir.x };

        // wasd двигает игрока
        // w s вперед назад по направлению камеры
        // a d влево вправо по перпендикуляру к направлению это стрейф
        if (IsKeyDown(KEY_W)) try_move(player, level, game, player.dir.x * speed, player.dir.y * speed);
        if (IsKeyDown(KEY_S)) try_move(player, level, game, -player.dir.x * speed, -player.dir.y * speed);
        if (IsKeyDown(KEY_A)) try_move(player, level, game, -side_dir.x * speed, -side_dir.y * speed);
        if (IsKeyDown(KEY_D)) try_move(player, level, game, side_dir.x * speed, side_dir.y * speed);

        // мышь крутит камеру
        // поворачиваем и направление и плоскость на одинаковый угол
        // чтобы они всегда оставались перпендикулярны
        float turn = GetMouseDelta().x * MOUSE_SENS;
        player.dir = Vector2Rotate(player.dir, turn);
        player.plane = Vector2Rotate(player.plane, turn);

        // update_frame гарантирует правильный порядок
        // intensities - dynamic
        game.lighting.update_frame(game.lights, time_sec);
    }

    // -=-=-=-=-=-=-=-= helpers рендеров -=-=-=-=-=-=-=-=

    // вычисляем данные строки пола/потолка
    // row_dist < 0 значит строка невалидна (знаменатель слишком мал)
    SurfaceRow calc_surface_row(const RenderCtx& rc, int scr_y) {
        SurfaceRow row;

        float denom = std::abs(rc.height_f - 2.0f * scr_y);
        if (denom < SURFACE_DENOM_EPS) {
            return row;
        }

        row.row_dist = rc.height_f / denom;
        row.step_x = row.row_dist * rc.base_step_x;
        row.step_y = row.row_dist * rc.base_step_y;
        row.world_x = rc.player_pos.x + row.row_dist * rc.ray_left_x;
        row.world_y = rc.player_pos.y + row.row_dist * rc.ray_left_y;
        return row;
    }

    // tex_x с учетом зеркалирования
    // side == 0 зеркалим когда луч идет вправо
    // side == 1 зеркалим когда луч идет вверх
    int calc_tex_x(const CpuTex& tex, float u, int side,
        float ray_dir_x, float ray_dir_y) {
        int tx = std::clamp((int)(u * tex.w), 0, tex.w - 1);

        bool mirror = false;
        if (side == 0 and ray_dir_x > 0.0f) mirror = true;
        if (side == 1 and ray_dir_y < 0.0f) mirror = true;
        if (mirror) tx = tex.w - tx - 1;

        return tx;
    }

    // начальная настройка dda по одной оси
    void dda_init_axis(float ray_dir, float pos, int cell,
        float& delta, int& step, float& side_dist) {

        if (std::abs(ray_dir) >= RAY_DIR_EPS) {
            delta = std::abs(1.0f / ray_dir);
        }
        else {
            delta = MAX_DEPTH;
        }

        if (ray_dir < 0.0f) {
            step = -1;
            side_dist = (pos - cell) * delta;
        }
        else {
            step = 1;
            side_dist = (cell + 1.0f - pos) * delta;
        }
    }

    // рисуем один вертикальный столбец текстуры
    // tex_x уже вычислен снаружи с зеркалированием
    void draw_column(const RenderCtx& rc, const CpuTex& tex, int scr_x,
        float dist, int tex_x, float light, float fog, bool use_alpha) {

        // считаем высоту поверхности на экране
        // чем ближе тем выше
        int line_height = std::max((int)(rc.height_f / dist), 1);

        int top = rc.half_h - line_height / 2;
        int draw_y0 = std::max(0, top);
        int draw_y1 = std::min(rc.height, top + line_height);
        if (draw_y0 >= draw_y1) return;

        // если туман полный поверхность все равно нужно закрасить
        // иначе она исчезнет и будет видно небо или фон за ней
        if (fog >= 1.0f) {
            for (int scr_y = draw_y0; scr_y < draw_y1; scr_y += rc.quality) {
                put_block(rc, scr_x, scr_y, FOG_CLR);
            }
            return;
        }

        // предвычисляем коэффициент чтобы быстрее переводить y экрана в v текстуры
        float inv_line = (float)tex.h / (float)line_height;

        // рисуем вертикальный столбец пиксель за пикселем
        for (int scr_y = draw_y0; scr_y < draw_y1; scr_y += rc.quality) {
            int tex_y = std::clamp((int)((scr_y - top) * inv_line), 0, tex.h - 1);
            Color src = tex.pixels[tex_y * tex.w + tex_x];

            if (use_alpha) {
                // прозрачный пиксель это дырка в двери
                // через нее видно стену которую мы уже нарисовали
                if (src.a == 0) continue;
            }

            put_block(rc, scr_x, scr_y, apply_shade(src, light, fog));
        }
    }

    // -=-=-=-=-=-=-=-= рендер небо -=-=-=-=-=-=-=-=

    // небо рисуем только в верхней половине экрана
    // нижнюю все равно закроют пол потолок и стены
    // верхняя полоса это градиент неба
    // нижняя полоса это панорама горизонта
    void render_sky(const RenderCtx& rc) {
        const CpuTex& sky_top = rc.assets->sky_top;
        const CpuTex& sky_side = rc.assets->sky_side;

        int band_h = rc.half_h / SKY_DIV;
        int side_h = rc.half_h - band_h;

        // считаем шаг текстуры по y один раз чтобы не делить в каждом пикселе
        float inv_band = 0.0f;
        float inv_side = 0.0f;

        if (band_h > 0) inv_band = (float)sky_top.h / band_h;
        if (side_h > 0) inv_side = (float)sky_side.h / side_h;

        for (int scr_x = 0; scr_x < rc.width; scr_x += rc.quality) {
            // для каждого столбца экрана считаем угол поворота
            // по этому углу выбираем нужный кусок панорамной текстуры
            float cam_x = 2.0f * scr_x / rc.width_f - 1.0f;
            float yaw = std::atan2(
                rc.view_dir.y + rc.view_plane.y * cam_x,
                rc.view_dir.x + rc.view_plane.x * cam_x
            ) * INV_2PI + 0.5f;

            int top_tx = wrap_coord(yaw, sky_top.w);
            int side_tx = wrap_coord(yaw, sky_side.w);

            // рисуем верхнюю полосу неба
            if (band_h > 0) {
                for (int scr_y = 0; scr_y < band_h; scr_y += rc.quality) {
                    int tex_y = std::min((int)(scr_y * inv_band), sky_top.h - 1);
                    put_block(rc, scr_x, scr_y, sky_top.pixels[tex_y * sky_top.w + top_tx]);
                }
            }

            // рисуем нижнюю полосу неба у горизонта
            if (side_h > 0) {
                for (int scr_y = band_h; scr_y < rc.half_h; scr_y += rc.quality) {
                    int tex_y = std::min((int)((scr_y - band_h) * inv_side), sky_side.h - 1);
                    put_block(rc, scr_x, scr_y, sky_side.pixels[tex_y * sky_side.w + side_tx]);
                }
            }
        }
    }

    // потолок рисуем только там где он есть в комнатах
    // где потолка нет там уже нарисовано небо и мы его не трогаем
    void render_ceiling(const RenderCtx& rc) {
        for (int scr_y = 0; scr_y < rc.half_h; scr_y += rc.quality) {
            SurfaceRow row = calc_surface_row(rc, scr_y);
            if (row.row_dist < 0.0f) continue;

            float fog = calc_fog(row.row_dist);
            bool full_fog = (row.row_dist > FOG_FAR);

            for (int scr_x = 0; scr_x < rc.width; scr_x += rc.quality) {
                int cell_x = std::clamp((int)row.world_x, 0, MAP_W - 1);
                int cell_y = std::clamp((int)row.world_y, 0, MAP_H - 1);

                // рисуем потолок только в тех клетках где он реально есть
                if (rc.level->ceilings[cell_y][cell_x]) {
                    if (full_fog) {
                        // если строка слишком далеко в тумане
                        // потолок красим цветом тумана только там где он реально есть
                        // иначе в дальних комнатах потолок будет "превращаться в небо"
                        put_block(rc, scr_x, scr_y, FOG_CLR);
                    }
                    else {
                        Color texel = sample_tex(rc.assets->ceiling, row.world_x, row.world_y);
                        put_block(rc, scr_x, scr_y, apply_shade(texel,
                            rc.game->lighting.sample(row.world_x, row.world_y), fog));
                    }
                }

                row.world_x += row.step_x;
                row.world_y += row.step_y;
            }
        }
    }

    // пол рисуем везде а за пределами тумана просто заливаем цветом тумана
    void render_floor(const RenderCtx& rc) {
        for (int scr_y = rc.half_h + 1; scr_y < rc.height; scr_y += rc.quality) {
            SurfaceRow row = calc_surface_row(rc, scr_y);
            if (row.row_dist < 0.0f) continue;

            // если строка слишком далеко в тумане
            // пол просто красим цветом тумана
            if (row.row_dist > FOG_FAR) {
                for (int scr_x = 0; scr_x < rc.width; scr_x += rc.quality) {
                    put_block(rc, scr_x, scr_y, FOG_CLR);
                }
                continue;
            }

            float fog = calc_fog(row.row_dist);

            for (int scr_x = 0; scr_x < rc.width; scr_x += rc.quality) {
                // для пола используем несколько разных текстур
                // какую именно определяем по мировым координатам
                // чтобы получился красивый чередующийся узор
                int tile_x = (int)std::floor(row.world_x * INV_TILE);
                int tile_y = (int)std::floor(row.world_y * INV_TILE);
                int floor_idx = (tile_x + tile_y) % FLOOR_TEX_N;
                if (floor_idx < 0) floor_idx += FLOOR_TEX_N;

                Color texel = sample_tex(rc.assets->floors[floor_idx], row.world_x, row.world_y);
                put_block(rc, scr_x, scr_y, apply_shade(texel,
                    rc.game->lighting.sample(row.world_x, row.world_y), fog));

                row.world_x += row.step_x;
                row.world_y += row.step_y;
            }
        }
    }

    // для каждого столбца экрана кидаем луч и ищем где он попадет в стену
    // это классический dda алгоритм из wolfenstein 3d
    // когда нашли стену считаем расстояние рисуем текстуру и пишем глубину
    // глубина нужна чтобы потом спрайты не рисовались сквозь стены
    //
    // дверь не останавливает луч потому что у нее может быть окно через которое видно дальше по лучу
    // поэтому запоминаем ближайшую дверь и ближайшую стену за ней отдельно
    // если дверь ближе стены рисуем сначала стену как фон потом дверь поверх с альфой
    // если стена ближе рисуем только стену
    //
    void render_walls(const RenderCtx& rc) {
        // позиция игрока в виде массива для вычисления расстояния по осям
        // не зависит от столбца поэтому вычисляем один раз перед циклом
        float player_axis[2] = { rc.player_pos.x, rc.player_pos.y };

        for (int scr_x = 0; scr_x < rc.width; scr_x += rc.quality) {
            int depth_end = std::min(scr_x + rc.quality, rc.width);

            // считаем направление луча для этого столбца
            // cam_x это позиция столбца от -1 слева до +1 справа
            float cam_x = 2.0f * scr_x / rc.width_f - 1.0f;
            float ray_dir_x = rc.view_dir.x + rc.view_plane.x * cam_x;
            float ray_dir_y = rc.view_dir.y + rc.view_plane.y * cam_x;

            float ray_dir_axis[2] = { ray_dir_x, ray_dir_y };

            // начинаем с клетки в которой стоит игрок
            int cell_x = (int)rc.player_pos.x;
            int cell_y = (int)rc.player_pos.y;

            float delta_x = 0.0f;
            float delta_y = 0.0f;
            float side_dist_x = 0.0f;
            float side_dist_y = 0.0f;
            int step_x = 0;
            int step_y = 0;

            // инициализация dda по обеим осям
            dda_init_axis(ray_dir_x, rc.player_pos.x, cell_x, delta_x, step_x, side_dist_x);
            dda_init_axis(ray_dir_y, rc.player_pos.y, cell_y, delta_y, step_y, side_dist_y);

            int hit_side = 0;

            // ближайшая дверь по лучу
            const DoorObj* hit_door = nullptr;
            float door_dist = MAX_DEPTH;
            float door_tex_u = 0.0f;
            int door_hit_side = 0;

            // ближайшая стена по лучу
            bool wall_found = false;
            float wall_dist = MAX_DEPTH;
            float wall_tex_u = 0.0f;
            int wall_tex_id = 0;

            // массивы для вычисления расстояния и света стены
            // перезаписываются внутри while при wall hit
            // инициализация нулями только для подавления warning
            int cell_axis[2] = { 0, 0 };
            int step_axis[2] = { step_x, step_y };

            // шагаем по клеткам карты пока не найдем стену или не выйдем за карту
            // дверь не останавливает обход потому что за ней может быть стена
            // которую нужно показать через окно в двери
            while (true) {
                // выбираем по какой оси шагнуть: берем ту где ближе
                if (side_dist_x < side_dist_y) {
                    side_dist_x += delta_x;
                    cell_x += step_x;
                    hit_side = 0;
                }
                else {
                    side_dist_y += delta_y;
                    cell_y += step_y;
                    hit_side = 1;
                }

                // если вышли за карту луч ни во что не попал
                if (out_of_map(cell_x, cell_y)) {
                    break;
                }

                // проверяем дверь в этой клетке
                // запоминаем только первую по лучу потому что вторая за ней не видна
                if (!hit_door) {
                    const DoorObj* door = find_door(*rc.game, cell_x, cell_y);

                    if (door) {
                        float hit_dist = 0.0f;
                        float hit_u = 0.0f;
                        int hit_s = 0;

                        if (ray_hit_door(*door,
                            rc.player_pos.x, rc.player_pos.y,
                            ray_dir_x, ray_dir_y,
                            hit_dist, hit_u, hit_s))
                        {
                            hit_door = door;
                            door_dist = hit_dist;
                            door_tex_u = hit_u;
                            door_hit_side = hit_s;
                        }
                    }
                }

                // проверяем стену в этой клетке
                // первая стена по лучу это конец обхода
                int wall_id = rc.level->walls[cell_y][cell_x];
                if (wall_id <= 0) continue;

                wall_found = true;
                wall_tex_id = wall_id - 1;

                // считаем перпендикулярное расстояние до стены
                // именно перпендикулярное а не реальное чтобы не было эффекта рыбьего глаза
                cell_axis[0] = cell_x;
                cell_axis[1] = cell_y;

                wall_dist = (cell_axis[hit_side] - player_axis[hit_side] + (1 - step_axis[hit_side]) * 0.5f)
                    / ray_dir_axis[hit_side];

                // не даем расстоянию быть слишком маленьким
                wall_dist = std::max(wall_dist, MIN_WALL_DIST);

                // считаем в какое место внутри клетки попал луч
                // это дает нам u координату текстуры
                int side_axis = 1 - hit_side;
                wall_tex_u = player_axis[side_axis] + wall_dist * ray_dir_axis[side_axis];
                wall_tex_u -= std::floor(wall_tex_u);

                break;
            }

            // записываем глубину чтобы спрайты знали что тут препятствие
            // берем ближайшее из двери и стены
            // если луч ни во что не попал оба останутся MAX_DEPTH
            float front_dist = std::min(door_dist, wall_dist);

            bool door_in_front = hit_door and (!wall_found or door_dist < wall_dist);

            if (hit_door or wall_found) {
                // рисуем стену если она есть
                // стена рисуется всегда когда она видна даже если дверь впереди
                // потому что через прозрачные пиксели двери нужно видеть стену за ней
                if (wall_found and wall_tex_id < WALL_TEX_N) {
                    // свет стены берем из клетки перед ней откуда пришел луч
                    // потому что игрок видит лицевую сторону которая освещается
                    // светом из соседнего пространства а не изнутри самой стены
                    float light_pos[2] = { (float)cell_axis[0] + 0.5f, (float)cell_axis[1] + 0.5f };
                    light_pos[hit_side] += (float)(-step_axis[hit_side]);

                    float wall_light = rc.game->lighting.sample(light_pos[0], light_pos[1]);

                    // стены по одной оси чуть темнее чтобы читался объем как в wolf3d
                    if (hit_side == 1) wall_light *= WALL_SIDE_SHADE;

                    // для некоторых сторон разворачиваем текстуру зеркально
                    // чтобы в углах стен текстуры стыковались красиво
                    const CpuTex& wall_tex = rc.assets->walls[wall_tex_id];
                    int tex_x = calc_tex_x(wall_tex, wall_tex_u, hit_side, ray_dir_x, ray_dir_y);

                    draw_column(rc, wall_tex, scr_x,
                        wall_dist, tex_x, wall_light, calc_fog(wall_dist),
                        false);
                }

                // если дверь впереди рисуем поверх стены
                // прозрачные пиксели двери это окно через которое видно
                // то что уже нарисовано позади (стену или фон)
                if (door_in_front) {
                    // свет двери берем из ее собственной клетки
                    float door_light = rc.game->lighting.sample(
                        (float)hit_door->cell_x + 0.5f, (float)hit_door->cell_y + 0.5f);
                    if (door_hit_side == 1) door_light *= WALL_SIDE_SHADE;

                    // зеркалим чтобы текстура читалась одинаково с обеих сторон
                    //
                    // door_hit_side кодирует ориентацию
                    // 0 = vertical
                    // 1 = horizontal
                    //
                    const CpuTex& door_tex = rc.assets->doors[hit_door->tex_id];
                    int dtex_x = calc_tex_x(door_tex, door_tex_u, door_hit_side, ray_dir_x, ray_dir_y);

                    draw_column(rc, door_tex, scr_x,
                        door_dist, dtex_x, door_light, calc_fog(door_dist),
                        true);
                }
            }

            std::fill(rc.depth_buf + scr_x, rc.depth_buf + depth_end, front_dist);
        }
    }

    // спрайты это плоские картинки в мире которые всегда повернуты к камере
    // сначала сортируем от дальних к ближним чтобы ближние рисовались поверх
    // для каждого столбца спрайта проверяем глубину
    // если стена ближе то этот столбец спрайта не рисуем
    //
    // кадр анимации вычисляется здесь на лету по времени
    // base_tex_id не меняется и всегда указывает на первый кадр
    //
    // сортируем индексы а не сами спрайты чтобы рендер не менял порядок в game.sprites
    // массивы order и dist_sq передаются снаружи чтобы не аллоцировать каждый кадр
    //
    void render_sprites(const RenderCtx& rc, std::vector<int>& order,
        std::vector<float>& dist_sq_buf) {

        const std::vector<SpriteObj>& sprites = rc.game->sprites;
        if (sprites.empty()) return;

        int count = (int)sprites.size();

        // предвычисляем расстояния один раз
        // чтобы не считать заново при каждом сравнении в сортировке
        dist_sq_buf.resize(count);
        for (int i = 0; i < count; ++i) {
            float dx = rc.player_pos.x - sprites[i].pos.x;
            float dy = rc.player_pos.y - sprites[i].pos.y;
            dist_sq_buf[i] = dx * dx + dy * dy;
        }

        // собираем массив индексов для сортировки
        // resize не должен аллоцировать если capacity достаточно
        order.resize(count);
        for (int i = 0; i < count; ++i) order[i] = i;

        // сортируем спрайты по расстоянию до игрока
        // дальние первые ближние последние
        std::sort(order.begin(), order.end(),
            [&dist_sq_buf](int a, int b) {
                return dist_sq_buf[a] > dist_sq_buf[b];
            });

        // обратный определитель матрицы камеры
        // нужен чтобы переводить мировые координаты в экранные
        // dir и plane всегда перпендикулярны после поворота поэтому определитель не нулевой
        float inv_det = 1.0f / (rc.view_plane.x * rc.view_dir.y - rc.view_dir.x * rc.view_plane.y);

        for (int oi = 0; oi < count; ++oi) {
            const SpriteObj& sprite = sprites[order[oi]];

            // вычисляем текущий кадр анимации
            // для статичных спрайтов anim_frames == 1 и tex_id == base_tex_id
            int tex_id = sprite.base_tex_id;

            if (sprite.anim_frames > 1) {
                float t = rc.time_sec + sprite.anim_phase;
                int frame = (int)(t * sprite.anim_fps) % sprite.anim_frames;

                // защита от отрицательного остатка
                if (frame < 0) frame += sprite.anim_frames;

                tex_id += frame;
            }

            if (tex_id < 0 or tex_id >= SPRITE_TEX_N) continue;

            // позиция спрайта относительно игрока
            float rel_x = sprite.pos.x - rc.player_pos.x;
            float rel_y = sprite.pos.y - rc.player_pos.y;

            // переводим в координаты камеры
            // cam_x это смещение влево вправо на экране
            // depth это глубина как далеко от камеры
            float cam_x = inv_det * (rc.view_dir.y * rel_x - rc.view_dir.x * rel_y);
            float depth = inv_det * (-rc.view_plane.y * rel_x + rc.view_plane.x * rel_y);

            // если спрайт за спиной то не рисуем
            if (depth <= 0.0f) continue;

            // emissive спрайты (факелы) всегда рисуем с полной яркостью
            // чтобы сам факел не дрожал от своего же мерцающего света
            float spr_light = 1.0f;
            if (!sprite.emissive) spr_light = rc.game->lighting.sample(sprite.pos.x, sprite.pos.y);

            // свет и туман одинаковые для всего спрайта
            // потому что он стоит в одной точке мира
            float spr_fog = calc_fog(depth);

            // когда туман полный можно не рисовать
            // так спрайты плавно исчезают в тумане без жесткого обрыва
            if (spr_fog >= 1.0f) continue;

            // считаем в какой столбец экрана попадает центр спрайта
            int center_x = (int)(rc.width_f * 0.5f * (1.0f + cam_x / depth));

            // считаем размер спрайта на экране
            float base_size = rc.height_f / depth;
            int sprite_size = (int)(base_size * sprite.scale);
            if (sprite_size <= 0) continue;

            // низ спрайта привязан к уровню пола
            int ground_y = rc.half_h + (int)(base_size * 0.5f);
            const CpuTex& texture = rc.assets->sprites[tex_id];

            int left = center_x - sprite_size / 2;
            int top = ground_y - sprite_size;

            // обрезаем по границам экрана
            int draw_x0 = std::max(0, left);
            int draw_x1 = std::min(rc.width, left + sprite_size);
            int draw_y0 = std::max(0, top);
            int draw_y1 = std::min(rc.height, ground_y);

            // если после обрезки рисовать нечего пропускаем
            if (draw_x0 >= draw_x1 or draw_y0 >= draw_y1) continue;

            for (int scr_x = draw_x0; scr_x < draw_x1; scr_x += rc.quality) {
                // проверяем глубину если стена ближе то этот столбец не рисуем
                if (depth >= rc.depth_buf[scr_x]) continue;

                // clamp защищает от выхода за текстуру при округлении на краях спрайта
                int tex_u = std::clamp((scr_x - left) * texture.w / sprite_size, 0, texture.w - 1);

                for (int scr_y = draw_y0; scr_y < draw_y1; scr_y += rc.quality) {
                    int tex_v = std::clamp((scr_y - top) * texture.h / sprite_size, 0, texture.h - 1);
                    Color texel = texture.pixels[tex_v * texture.w + tex_u];

                    // прозрачные пиксели не рисуем
                    if (texel.a == 0) continue;

                    put_block(rc, scr_x, scr_y, apply_shade(texel, spr_light, spr_fog));
                }
            }
        }
    }

    // -=-=-=-=-=-=-=-= кадр -=-=-=-=-=-=-=-=

    // рисуем все слои в правильном порядке
    // сначала небо потом строка горизонта потом потолок потом пол потом стены и последними спрайты
    // каждый следующий слой рисуется поверх предыдущего
    void render_frame(const GameState& game, const Assets& assets, const LevelData& level,
        std::vector<Color>& pixels, std::vector<float>& depth,
        std::vector<int>& sprite_order, std::vector<float>& sprite_dist, float time_sec) {

        const Player& player = game.player;

        RenderCtx rc = {};
        rc.game = &game;
        rc.assets = &assets;
        rc.level = &level;
        rc.framebuf = pixels.data();
        rc.depth_buf = depth.data();
        rc.width = SCREEN_W;
        rc.height = SCREEN_H;
        rc.half_h = SCREEN_H / 2;
        rc.quality = RENDER_Q;
        rc.time_sec = time_sec;
        rc.width_f = (float)SCREEN_W;
        rc.height_f = (float)SCREEN_H;
        rc.player_pos = player.pos;
        rc.view_dir = player.dir;
        rc.view_plane = player.plane;

        // base_step включает RENDER_Q потому что floor/ceiling шагают
        // по scr_x с шагом quality и world координата должна прыгать на столько же
        rc.base_step_x = 2.0f * rc.view_plane.x / rc.width_f * RENDER_Q;
        rc.base_step_y = 2.0f * rc.view_plane.y / rc.width_f * RENDER_Q;
        rc.ray_left_x = rc.view_dir.x - rc.view_plane.x;
        rc.ray_left_y = rc.view_dir.y - rc.view_plane.y;

        render_sky(rc);

        // закрываем строку горизонта
        // иначе она не рисуется ни небом ни полом
        for (int scr_x = 0; scr_x < rc.width; scr_x += rc.quality) {
            put_block(rc, scr_x, rc.half_h, FOG_CLR);
        }

        render_ceiling(rc);
        render_floor(rc);
        render_walls(rc);
        render_sprites(rc, sprite_order, sprite_dist);
    }

} // namespace

// -=-=-=-=-=-=-=-= точка входа -=-=-=-=-=-=-=-=

int main() {
    InitWindow(SCREEN_W, SCREEN_H, "raycasting");
    SetTargetFPS(TARGET_FPS);
    DisableCursor();

    Assets assets;
    load_all_assets(assets);

    GameState game;
    init_game(game, LEVEL_1);

    // массив пикселей в оперативке
    // каждый кадр рисуем сюда а потом отправляем на видеокарту
    std::vector<Color> pixels(SCREEN_W * SCREEN_H);

    // для каждого столбца экрана храним расстояние до ближайшей стены
    // спрайты смотрят сюда чтобы не рисоваться поверх стен
    std::vector<float> depth(SCREEN_W);

    // массив индексов для сортировки спрайтов
    // чтобы не аллоцировать каждый кадр
    std::vector<int> sprite_order;

    // массив расстояний до спрайтов для сортировки
    // предвычисляем один раз за кадр вместо пересчета при каждом сравнении
    std::vector<float> sprite_dist;

    // создаем текстуру на видеокарте
    // каждый кадр будем заливать в нее наш массив пикселей
    Image img = GenImageColor(SCREEN_W, SCREEN_H, BLACK);
    Texture2D screen_tex = LoadTextureFromImage(img);
    UnloadImage(img);

    while (!WindowShouldClose()) {
        float dt = std::min(GetFrameTime(), MAX_DT);
        float time_sec = (float)GetTime();

        update_game(game, LEVEL_1, dt, time_sec);
        render_frame(game, assets, LEVEL_1, pixels, depth, sprite_order, sprite_dist, time_sec);

        // копируем наши пиксели из оперативки на видеокарту
        UpdateTexture(screen_tex, pixels.data());

        BeginDrawing();
        ClearBackground(BLACK);
        DrawTexture(screen_tex, 0, 0, WHITE);
        DrawFPS(10, 10);
        EndDrawing();
    }

    UnloadTexture(screen_tex);
    CloseWindow();
    return 0;
}