#include "raylib-cpp.hpp"
#include <vector>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <algorithm>

// ---------------------------------
// Grid & constants
// ---------------------------------
const int TILE_SIZE   = 32;
const int GRID_WIDTH  = 25;
const int GRID_HEIGHT = 18;

const int SCREEN_W = GRID_WIDTH * TILE_SIZE;
const int SCREEN_H = GRID_HEIGHT * TILE_SIZE;

const int START_LIVES = 3;
const int RESPAWN_DELAY = 180;   // 3 seconds @ 60 FPS
const int DEATH_FLASH_TIME = 30; // 0.5 seconds @ 60 FPS

// Tunnel directions
enum class TunnelDirection { HORIZONTAL, VERTICAL, NONE };

// ---------------------------------
// Helpers
// ---------------------------------
static Rectangle MakeNormalizedRect(float x, float y, float w, float h) {
    Rectangle r { x, y, w, h };
    if (r.width < 0) { r.x += r.width; r.width *= -1; }
    if (r.height < 0){ r.y += r.height; r.height *= -1; }
    return r;
}

static bool Button(const char* label, Rectangle bounds) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, bounds);
    DrawRectangleRec(bounds, hover ? DARKGRAY : GRAY);
    int tw = MeasureText(label, 20);
    DrawText(label, (int)(bounds.x + (bounds.width - tw)/2), (int)(bounds.y + (bounds.height-20)/2), 20, WHITE);
    return hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

// ---------------------------------
// Tunnel structure
// ---------------------------------
struct Tunnel {
    int startX, startY;
    int length;
    TunnelDirection direction;
    bool dug = true; // Tunnels are visible from the start
    bool activated = false; // Whether player has entered this tunnel
    
    Tunnel(int x, int y, int len, TunnelDirection dir)
        : startX(x), startY(y), length(len), direction(dir) {}
    
    bool Contains(int x, int y) const {
        if (direction == TunnelDirection::HORIZONTAL) {
            return y == startY && x >= startX && x < startX + length;
        } else if (direction == TunnelDirection::VERTICAL) {
            return x == startX && y >= startY && y < startY + length;
        }
        return false;
    }
    
    bool Intersects(const Tunnel& other) const {
        if (direction == TunnelDirection::HORIZONTAL && other.direction == TunnelDirection::HORIZONTAL) {
            // Both horizontal - check if on same row and segments overlap
            if (startY != other.startY) return false;
            return !(startX + length <= other.startX || other.startX + other.length <= startX);
        }
        else if (direction == TunnelDirection::VERTICAL && other.direction == TunnelDirection::VERTICAL) {
            // Both vertical - check if on same column and segments overlap
            if (startX != other.startX) return false;
            return !(startY + length <= other.startY || other.startY + other.length <= startY);
        }
        else {
            // One horizontal, one vertical - check if they cross
            if (direction == TunnelDirection::HORIZONTAL) {
                // This is horizontal, other is vertical
                return (other.startX >= startX && other.startX < startX + length) &&
                       (startY >= other.startY && startY < other.startY + other.length);
            } else {
                // This is vertical, other is horizontal
                return (startX >= other.startX && startX < other.startX + other.length) &&
                       (other.startY >= startY && other.startY < startY + length);
            }
        }
    }
    
    void Draw() const {
        Color color = BLACK; // Tunnels are always black (dug)
        if (direction == TunnelDirection::HORIZONTAL) {
            DrawRectangle(startX * TILE_SIZE, startY * TILE_SIZE,
                         length * TILE_SIZE, TILE_SIZE, color);
        } else {
            DrawRectangle(startX * TILE_SIZE, startY * TILE_SIZE,
                         TILE_SIZE, length * TILE_SIZE, color);
        }
    }
};

// ---------------------------------
// Game Objects
// ---------------------------------
class Player {
public:
    raylib::Vector2 pos;
    int size = TILE_SIZE;
    float speed = 2.0f;

    bool alive = true;
    int lives  = START_LIVES;

    // Harpoon
    bool hasHarpoon = false;
    raylib::Vector2 harpoonDir{1,0};
    int harpoonTimer = 0;         // frames remaining
    int score = 0;

    // Death animation
    int deathFlashTimer = 0;

    Player(int x, int y) { pos = raylib::Vector2((float)x, (float)y); }

    void ResetTo(int x, int y) {
        pos = raylib::Vector2((float)x, (float)y);
        alive = true;
        hasHarpoon = false;
        harpoonTimer = 0;
        harpoonDir = raylib::Vector2(1,0);
        deathFlashTimer = 0;
    }

    void Move() {
        if (IsKeyDown(KEY_RIGHT)) { pos.x += speed; harpoonDir = raylib::Vector2(1, 0); }
        if (IsKeyDown(KEY_LEFT))  { pos.x -= speed; harpoonDir = raylib::Vector2(-1, 0); }
        if (IsKeyDown(KEY_UP))    { pos.y -= speed; harpoonDir = raylib::Vector2(0, -1); }
        if (IsKeyDown(KEY_DOWN))  { pos.y += speed; harpoonDir = raylib::Vector2(0, 1); }

        // Keep in window
        if (pos.x < 0) pos.x = 0;
        if (pos.y < 0) pos.y = 0;
        if (pos.x > SCREEN_W - size) pos.x = SCREEN_W - size;
        if (pos.y > SCREEN_H - size) pos.y = SCREEN_H - size;

        // Fire harpoon
        if (IsKeyPressed(KEY_SPACE)) {
            hasHarpoon = true;
            harpoonTimer = 15; // visible frames
        }
    }

    void Draw() {
        Color col = BLUE;
        if (deathFlashTimer > 0) col = RED;

        DrawRectangle((int)pos.x, (int)pos.y, size, size, col);

        if (hasHarpoon && harpoonTimer > 0) {
            DrawLine((int)(pos.x + size/2), (int)(pos.y + size/2),
                     (int)(pos.x + size/2 + harpoonDir.x*50),
                     (int)(pos.y + size/2 + harpoonDir.y*50),
                     RAYWHITE);
            harpoonTimer--;
            if (harpoonTimer <= 0) hasHarpoon = false;
        }

        if (deathFlashTimer > 0) deathFlashTimer--;
    }

    Rectangle Bounds() const {
        return Rectangle{ pos.x, pos.y, (float)size, (float)size };
    }
};

class Monster {
public:
    raylib::Vector2 pos;
    int size = TILE_SIZE;
    float speed = 0.5f;
    float chaseSpeed = 1.2f; // Faster when chasing
    bool alive = true;
    bool inTunnel = true;
    bool chasing = false;
    Tunnel* homeTunnel = nullptr;
    int direction = 1; // 1 for right/down, -1 for left/up

    Monster(int x, int y, Tunnel* tunnel) {
        pos = raylib::Vector2((float)x, (float)y);
        homeTunnel = tunnel;
    }

    void MoveInTunnel() {
        if (!alive || !inTunnel) return;
        
        if (homeTunnel->direction == TunnelDirection::HORIZONTAL) {
            pos.x += speed * direction;
            
            // Check if reached tunnel end
            int gridX = static_cast<int>(pos.x) / TILE_SIZE;
            if (gridX <= homeTunnel->startX ||
                gridX >= homeTunnel->startX + homeTunnel->length - 1) {
                direction *= -1; // Reverse direction
            }
        } else {
            pos.y += speed * direction;
            
            // Check if reached tunnel end
            int gridY = static_cast<int>(pos.y) / TILE_SIZE;
            if (gridY <= homeTunnel->startY ||
                gridY >= homeTunnel->startY + homeTunnel->length - 1) {
                direction *= -1; // Reverse direction
            }
        }
    }

    void MoveTowards(const raylib::Vector2& target) {
        if (!alive || inTunnel) return;
        
        float currentSpeed = chasing ? chaseSpeed : speed;
        
        if (target.x > pos.x) pos.x += currentSpeed;
        else if (target.x < pos.x) pos.x -= currentSpeed;
        
        if (target.y > pos.y) pos.y += currentSpeed;
        else if (target.y < pos.y) pos.y -= currentSpeed;
    }

    void Draw() const {
        if (alive) {
            Color color = chasing ? MAROON : RED;
            DrawRectangle((int)pos.x, (int)pos.y, size, size, color);
        }
    }

    Rectangle Bounds() const {
        return Rectangle{ pos.x, pos.y, (float)size, (float)size };
    }
};

class Dragon {
public:
    raylib::Vector2 pos;
    int size = TILE_SIZE;
    float speed = 0.5f;
    float chaseSpeed = 1.5f; // Faster when chasing
    bool alive = true;
    bool inTunnel = true;
    bool chasing = false;
    Tunnel* homeTunnel = nullptr;
    int direction = 1; // 1 for right/down, -1 for left/up

    Dragon(int x, int y, Tunnel* tunnel) {
        pos = raylib::Vector2((float)x, (float)y);
        homeTunnel = tunnel;
    }

    void MoveInTunnel() {
        if (!alive || !inTunnel) return;
        
        if (homeTunnel->direction == TunnelDirection::HORIZONTAL) {
            pos.x += speed * direction;
            
            // Check if reached tunnel end
            int gridX = static_cast<int>(pos.x) / TILE_SIZE;
            if (gridX <= homeTunnel->startX ||
                gridX >= homeTunnel->startX + homeTunnel->length - 1) {
                direction *= -1; // Reverse direction
            }
        } else {
            pos.y += speed * direction;
            
            // Check if reached tunnel end
            int gridY = static_cast<int>(pos.y) / TILE_SIZE;
            if (gridY <= homeTunnel->startY ||
                gridY >= homeTunnel->startY + homeTunnel->length - 1) {
                direction *= -1; // Reverse direction
            }
        }
    }

    void MoveTowards(const raylib::Vector2& target) {
        if (!alive || inTunnel) return;
        
        float currentSpeed = chasing ? chaseSpeed : speed;
        
        if (target.x > pos.x) pos.x += currentSpeed;
        else if (target.x < pos.x) pos.x -= currentSpeed;
        
        if (target.y > pos.y) pos.y += currentSpeed;
        else if (target.y < pos.y) pos.y -= currentSpeed;
    }

    void Draw() const {
        if (!alive) return;
        
        Color color = chasing ? DARKGREEN : GREEN;
        Vector2 p1 { pos.x + size/2.0f, pos.y };
        Vector2 p2 { pos.x,              pos.y + (float)size };
        Vector2 p3 { pos.x + (float)size, pos.y + (float)size };
        DrawTriangle(p1, p2, p3, color);
    }

    Rectangle Bounds() const {
        return Rectangle{ pos.x, pos.y, (float)size, (float)size };
    }
};

class Fruit {
public:
    raylib::Vector2 pos;
    int size = TILE_SIZE;
    bool collected = false;

    Fruit(int x, int y) { pos = raylib::Vector2((float)x, (float)y); }

    void Draw() const {
        if (!collected) {
            DrawCircle((int)(pos.x + size/2), (int)(pos.y + size/2), size/2, LIME);
            DrawCircleLines((int)(pos.x + size/2), (int)(pos.y + size/2), size/2, DARKGREEN);
        }
    }

    Rectangle Bounds() const {
        return Rectangle{ pos.x, pos.y, (float)size, (float)size };
    }
};

// ---------------------------------
// Game state
// ---------------------------------
enum class GameState { SPLASH, PLAYING, GAMEOVER, WIN };

struct World {
    Player player{100,100};
    std::vector<Monster> monsters;
    std::vector<Dragon>  dragons;
    std::vector<Tunnel> tunnels;
    Fruit fruit{SCREEN_W/2 - TILE_SIZE/2, SCREEN_H/2 - TILE_SIZE/2};
    std::vector<std::vector<bool>> dug{ GRID_HEIGHT, std::vector<bool>(GRID_WIDTH, false) };
    GameState state = GameState::SPLASH;
    int highScore = 0;

    int respawnTimer = 0;

    void LoadHighScore() {
        std::ifstream in("highscore.txt");
        if (in) in >> highScore;
    }
    void SaveHighScore() {
        if (player.score > highScore) {
            highScore = player.score;
            std::ofstream out("highscore.txt");
            if (out) out << highScore;
        }
    }

    bool IsValidTunnel(const Tunnel& newTunnel) {
        // Check if tunnel is within bounds
        if (newTunnel.direction == TunnelDirection::HORIZONTAL) {
            if (newTunnel.startX < 1 || newTunnel.startX + newTunnel.length >= GRID_WIDTH - 1 ||
                newTunnel.startY < 1 || newTunnel.startY >= GRID_HEIGHT - 1) {
                return false;
            }
        } else {
            if (newTunnel.startX < 1 || newTunnel.startX >= GRID_WIDTH - 1 ||
                newTunnel.startY < 1 || newTunnel.startY + newTunnel.length >= GRID_HEIGHT - 1) {
                return false;
            }
        }
        
        // Check if tunnel doesn't intersect with existing tunnels
        for (const auto& tunnel : tunnels) {
            if (newTunnel.Intersects(tunnel)) {
                return false;
            }
        }
        
        return true;
    }

    void CreateTunnels() {
        tunnels.clear();
        
        // Create some horizontal tunnels
        int attempts = 0;
        while (tunnels.size() < 4 && attempts < 50) {
            int x = rand() % (GRID_WIDTH - 10) + 2;
            int y = rand() % (GRID_HEIGHT - 4) + 2;
            int length = rand() % 5 + 4; // 4-8 tiles long
            
            Tunnel newTunnel(x, y, length, TunnelDirection::HORIZONTAL);
            if (IsValidTunnel(newTunnel)) {
                tunnels.push_back(newTunnel);
            }
            attempts++;
        }
        
        // Create some vertical tunnels
        attempts = 0;
        while (tunnels.size() < 8 && attempts < 50) {
            int x = rand() % (GRID_WIDTH - 4) + 2;
            int y = rand() % (GRID_HEIGHT - 10) + 2;
            int length = rand() % 5 + 4; // 4-8 tiles long
            
            Tunnel newTunnel(x, y, length, TunnelDirection::VERTICAL);
            if (IsValidTunnel(newTunnel)) {
                tunnels.push_back(newTunnel);
            }
            attempts++;
        }
    }

    void ResetLevel() {
        for (auto& row : dug) std::fill(row.begin(), row.end(), false);
        player.ResetTo(100,100);
        monsters.clear();
        dragons.clear();
        
        CreateTunnels();
        
        // Mark tunnel areas as dug
        for (auto& tunnel : tunnels) {
            if (tunnel.direction == TunnelDirection::HORIZONTAL) {
                for (int x = tunnel.startX; x < tunnel.startX + tunnel.length; x++) {
                    dug[tunnel.startY][x] = true;
                }
            } else {
                for (int y = tunnel.startY; y < tunnel.startY + tunnel.length; y++) {
                    dug[y][tunnel.startX] = true;
                }
            }
            tunnel.activated = false;
        }
        
        // Place monsters in tunnels
        for (auto& tunnel : tunnels) {
            if (rand() % 2 == 0) { // 50% chance to place a monster in this tunnel
                int x, y;
                if (tunnel.direction == TunnelDirection::HORIZONTAL) {
                    x = (tunnel.startX + tunnel.length / 2) * TILE_SIZE;
                    y = tunnel.startY * TILE_SIZE;
                } else {
                    x = tunnel.startX * TILE_SIZE;
                    y = (tunnel.startY + tunnel.length / 2) * TILE_SIZE;
                }
                monsters.emplace_back(x, y, &tunnel);
            }
        }
        
        // Place dragons in remaining tunnels
        for (auto& tunnel : tunnels) {
            bool hasMonster = false;
            for (auto& monster : monsters) {
                if (monster.homeTunnel == &tunnel) {
                    hasMonster = true;
                    break;
                }
            }
            
            if (!hasMonster && rand() % 2 == 0) { // 50% chance to place a dragon in this tunnel
                int x, y;
                if (tunnel.direction == TunnelDirection::HORIZONTAL) {
                    x = (tunnel.startX + tunnel.length / 2) * TILE_SIZE;
                    y = tunnel.startY * TILE_SIZE;
                } else {
                    x = tunnel.startX * TILE_SIZE;
                    y = (tunnel.startY + tunnel.length / 2) * TILE_SIZE;
                }
                dragons.emplace_back(x, y, &tunnel);
            }
        }

        fruit.collected = false;
    }

    void ResetAll() {
        player.lives = START_LIVES;
        player.score = 0;
        player.alive = true;
        ResetLevel();
        state = GameState::SPLASH;
        respawnTimer = 0;
    }
    
    Tunnel* GetTunnelAt(int gridX, int gridY) {
        for (auto& tunnel : tunnels) {
            if (tunnel.Contains(gridX, gridY)) {
                return &tunnel;
            }
        }
        return nullptr;
    }
    
    void CheckTunnelActivation() {
        int playerGridX = static_cast<int>(player.pos.x / TILE_SIZE);
        int playerGridY = static_cast<int>(player.pos.y / TILE_SIZE);
        
        for (auto& tunnel : tunnels) {
            if (tunnel.Contains(playerGridX, playerGridY) && !tunnel.activated) {
                tunnel.activated = true;
                
                // Alert monsters and dragons in this tunnel
                for (auto &m : monsters) {
                    if (m.homeTunnel == &tunnel) {
                        m.inTunnel = false;
                        m.chasing = true;
                    }
                }
                for (auto &d : dragons) {
                    if (d.homeTunnel == &tunnel) {
                        d.inTunnel = false;
                        d.chasing = true;
                    }
                }
                
                // Debug message
                // std::cout << "Tunnel activated! Monsters/Dragons are now chasing!" << std::endl;
            }
        }
    }
};

// ---------------------------------
// Main
// ---------------------------------
int main() {
    srand((unsigned)time(nullptr));
    raylib::Window window(SCREEN_W, SCREEN_H, "Dig Dug with Tunnels");
    SetTargetFPS(60);

    World world;
    world.LoadHighScore();
    world.ResetAll();

    Rectangle restartBtn = { SCREEN_W/2.0f - 100, SCREEN_H/2.0f + 40, 200, 50 };

    while (!window.ShouldClose()) {
        // -------------------------
        // UPDATE
        // -------------------------
        if (world.state == GameState::SPLASH) {
            if (IsKeyPressed(KEY_ENTER)) {
                world.state = GameState::PLAYING;
                world.ResetLevel();
            }
        }
        else if (world.state == GameState::PLAYING) {
            if (world.respawnTimer > 0) {
                world.respawnTimer--;
                if (world.respawnTimer == 0) {
                    world.player.alive = true;
                    world.ResetLevel();
                }
            } else {
                // Normal updates only if not respawning
                world.player.Move();
                int gx = (int)(world.player.pos.x / TILE_SIZE);
                int gy = (int)(world.player.pos.y / TILE_SIZE);
                if (gy >= 0 && gy < GRID_HEIGHT && gx >= 0 && gx < GRID_WIDTH) {
                    world.dug[gy][gx] = true;
                }
                
                // Check if player entered any tunnels
                world.CheckTunnelActivation();

                // Move monsters and dragons
                for (auto &m : world.monsters) {
                    if (m.inTunnel) {
                        m.MoveInTunnel();
                    } else if (m.chasing) {
                        m.MoveTowards(world.player.pos);
                    }
                }
                for (auto &d : world.dragons) {
                    if (d.inTunnel) {
                        d.MoveInTunnel();
                    } else if (d.chasing) {
                        d.MoveTowards(world.player.pos);
                    }
                }

                // Check collisions with player
                for (auto &m : world.monsters)
                    if (m.alive && CheckCollisionRecs(world.player.Bounds(), m.Bounds()))
                        world.player.alive = false;

                for (auto &d : world.dragons)
                    if (d.alive && CheckCollisionRecs(world.player.Bounds(), d.Bounds()))
                        world.player.alive = false;

                // Handle harpoon
                if (world.player.hasHarpoon && world.player.harpoonTimer > 0) {
                    Rectangle harpoonRect;
                    if (world.player.harpoonDir.x != 0) {
                        float w = world.player.harpoonDir.x * 50;
                        harpoonRect = MakeNormalizedRect(
                            world.player.pos.x + world.player.size/2,
                            world.player.pos.y + world.player.size/2 - 2,
                            w, 4
                        );
                    } else {
                        float h = world.player.harpoonDir.y * 50;
                        harpoonRect = MakeNormalizedRect(
                            world.player.pos.x + world.player.size/2 - 2,
                            world.player.pos.y + world.player.size/2,
                            4, h
                        );
                    }

                    for (auto &m : world.monsters)
                        if (m.alive && CheckCollisionRecs(harpoonRect, m.Bounds())) {
                            m.alive = false;
                            world.player.score += 100;
                        }

                    for (auto &d : world.dragons)
                        if (d.alive && CheckCollisionRecs(harpoonRect, d.Bounds())) {
                            d.alive = false;
                            world.player.score += 200;
                        }
                }

                if (!world.fruit.collected && CheckCollisionRecs(world.player.Bounds(), world.fruit.Bounds())) {
                    world.fruit.collected = true;
                    world.player.score += 500;
                }

                if (!world.player.alive) {
                    world.player.lives--;
                    world.player.deathFlashTimer = DEATH_FLASH_TIME;
                    if (world.player.lives > 0) {
                        world.respawnTimer = RESPAWN_DELAY;
                    } else {
                        world.SaveHighScore();
                        world.state = GameState::GAMEOVER;
                    }
                }

                bool allMonstersDead = true;
                for (auto &m : world.monsters) if (m.alive) { allMonstersDead = false; break; }
                bool allDragonsDead = true;
                for (auto &d : world.dragons) if (d.alive) { allDragonsDead = false; break; }
                
                if (allMonstersDead && allDragonsDead) {
                    world.SaveHighScore();
                    world.state = GameState::WIN;
                }
            }
        }
        else { // GAMEOVER or WIN
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_R)) {
                world.ResetAll();
            }
        }

        // -------------------------
        // DRAW
        // -------------------------
        BeginDrawing();
        ClearBackground(BROWN);

        if (world.state == GameState::SPLASH) {
            ClearBackground(BLACK);
            const char* title = "DIG DUG (Tunnel Edition)";
            DrawText(title, SCREEN_W/2 - MeasureText(title, 32)/2, 120, 32, WHITE);
            DrawText("Arrow keys: Move & dig", SCREEN_W/2 - 150, 200, 20, RAYWHITE);
            DrawText("Space: Harpoon (kills red & green)", SCREEN_W/2 - 180, 230, 20, RAYWHITE);
            DrawText("Enter tunnels to release monsters!", SCREEN_W/2 - 180, 260, 20, RAYWHITE);
            DrawText("Press ENTER to Start", SCREEN_W/2 - 130, 320, 24, YELLOW);
            DrawText(TextFormat("High Score: %d", world.highScore), 20, 20, 20, GRAY);
        }
        else if (world.state == GameState::PLAYING) {
            // Draw tunnels first (as black areas)
            for (auto& tunnel : world.tunnels) {
                tunnel.Draw();
            }
            
            // Draw dug areas on top
            for (int y = 0; y < GRID_HEIGHT; y++) {
                for (int x = 0; x < GRID_WIDTH; x++) {
                    if (world.dug[y][x]) {
                        DrawRectangle(x*TILE_SIZE, y*TILE_SIZE, TILE_SIZE, TILE_SIZE, BLACK);
                    }
                }
            }

            world.player.Draw();
            for (auto &m : world.monsters) m.Draw();
            for (auto &d : world.dragons)  d.Draw();
            world.fruit.Draw();

            DrawText(TextFormat("Score: %i", world.player.score), 20, 20, 20, YELLOW);
            DrawText(TextFormat("High: %i", world.highScore), 20, 44, 18, GRAY);
            DrawText("Lives:", SCREEN_W - 160, 20, 20, WHITE);
            for (int i = 0; i < world.player.lives; ++i)
                DrawRectangle(SCREEN_W - 90 + i*22, 18, 18, 18, BLUE);

            if (world.respawnTimer > 0) {
                int secs = (world.respawnTimer / 60) + 1;
                const char* msg = TextFormat("Respawning in %d...", secs);
                DrawText(msg, SCREEN_W/2 - MeasureText(msg, 32)/2, SCREEN_H/2 - 16, 32, YELLOW);
            }
        }
        else if (world.state == GameState::GAMEOVER) {
            ClearBackground(BLACK);
            const char* msg = "GAME OVER";
            DrawText(msg, SCREEN_W/2 - MeasureText(msg, 40)/2, 160, 40, RED);
            DrawText(TextFormat("Final Score: %i", world.player.score), SCREEN_W/2 - 140, 220, 24, WHITE);
            DrawText(TextFormat("High Score:  %i", world.highScore),   SCREEN_W/2 - 140, 250, 24, GRAY);

            if (Button("Restart (Enter/R)", restartBtn) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_R)) {
                world.ResetAll();
            }
        }
        else if (world.state == GameState::WIN) {
            ClearBackground(BLACK);
            const char* msg = "YOU WIN!";
            DrawText(msg, SCREEN_W/2 - MeasureText(msg, 40)/2, 160, 40, GREEN);
            DrawText(TextFormat("Final Score: %i", world.player.score), SCREEN_W/2 - 140, 220, 24, WHITE);
            DrawText(TextFormat("High Score:  %i", world.highScore),   SCREEN_W/2 - 140, 250, 24, GRAY);

            if (Button("Restart (Enter/R)", restartBtn) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_R)) {
                world.ResetAll();
            }
        }

        EndDrawing();
    }

    return 0;
}
