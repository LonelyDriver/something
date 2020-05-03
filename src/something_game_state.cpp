#include "something_game_state.hpp"

const char *projectile_state_as_cstr(Projectile_State state)
{
    switch (state) {
    case Projectile_State::Ded: return "Ded";
    case Projectile_State::Active: return "Active";
    case Projectile_State::Poof: return "Poof";
    }

    assert(0 && "Incorrect Projectile_State");
}

SDL_Texture *render_text_as_texture(SDL_Renderer *renderer,
                                    TTF_Font *font,
                                    const char *text,
                                    SDL_Color color)
{
    SDL_Surface *surface = stec(TTF_RenderText_Blended(font, text, color));
    SDL_Texture *texture = stec(SDL_CreateTextureFromSurface(renderer, surface));
    SDL_FreeSurface(surface);
    return texture;
}

void render_texture(SDL_Renderer *renderer, SDL_Texture *texture, Vec2f p)
{
    int w, h;
    sec(SDL_QueryTexture(texture, NULL, NULL, &w, &h));
    SDL_Rect srcrect = {0, 0, w, h};
    SDL_Rect dstrect = {(int) floorf(p.x), (int) floorf(p.y), w, h};
    sec(SDL_RenderCopy(renderer, texture, &srcrect, &dstrect));
}

// TODO(#25): Turn displayf into println style
void displayf(SDL_Renderer *renderer, TTF_Font *font,
              SDL_Color color, Vec2f p,
              const char *format, ...)
{
    va_list args;
    va_start(args, format);

    char text[256];
    vsnprintf(text, sizeof(text), format, args);

    SDL_Texture *texture =
        render_text_as_texture(renderer, font, text, color);
    render_texture(renderer, texture, p);
    SDL_DestroyTexture(texture);

    va_end(args);
}

void Game_State::update(float dt)
{
    // Update Player's gun direction
    int mouse_x, mouse_y;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    entities[PLAYER_ENTITY_INDEX].point_gun_at(
        camera.to_world(vec2((float) mouse_x, (float) mouse_y)));

    // Enemy AI
    if (!debug) {
        for (size_t i = 0; i < ROOM_ROW_COUNT - 1; ++i) {
            size_t player_index = room_index_at(entities[PLAYER_ENTITY_INDEX].pos).unwrap;
            size_t enemy_index = room_index_at(entities[ENEMY_ENTITY_INDEX_OFFSET + i].pos).unwrap;
            if (player_index == enemy_index) {
                entities[ENEMY_ENTITY_INDEX_OFFSET + i].point_gun_at(
                    entities[PLAYER_ENTITY_INDEX].pos);
                entity_shoot({ENEMY_ENTITY_INDEX_OFFSET + i});
            }
        }
    }

    // Update All Entities
    for (size_t i = 0; i < ENTITIES_COUNT; ++i) {
        entities[i].update(gravity, dt);
    }

    // Update All Projectiles
    update_projectiles(dt);

    // Entities/Projectiles interaction
    for (size_t index = 0; index < PROJECTILES_COUNT; ++index) {
        auto projectile = projectiles + index;
        if (projectile->state != Projectile_State::Active) continue;

        for (size_t entity_index = 0;
             entity_index < ENTITIES_COUNT;
             ++entity_index)
        {
            auto entity = entities + entity_index;

            if (entity->state != Entity_State::Alive) continue;
            if (entity_index == projectile->shooter.unwrap) continue;

            if (rect_contains_vec2(entity->hitbox_world(), projectile->pos)) {
                projectile->state = Projectile_State::Poof;
                projectile->poof_animat.frame_current = 0;
                entity->kill();
            }
        }
    }
}

void Game_State::render(SDL_Renderer *renderer)
{
    auto index = room_index_at(entities[PLAYER_ENTITY_INDEX].pos);

    const int NEIGHBOR_ROOM_DIM_ALPHA = 200;

    if (index.unwrap > 0) {
        room_row[index.unwrap - 1].render(
            renderer,
            camera,
            ground_grass_texture,
            ground_texture,
            {0, 0, 0, NEIGHBOR_ROOM_DIM_ALPHA});
    }

    room_row[index.unwrap].render(
        renderer,
        camera,
        ground_grass_texture,
        ground_texture);

    if (index.unwrap + 1 < (int) ROOM_ROW_COUNT) {
        room_row[index.unwrap + 1].render(
            renderer,
            camera,
            ground_grass_texture,
            ground_texture,
            {0, 0, 0, NEIGHBOR_ROOM_DIM_ALPHA});
    }

    for (size_t i = 0; i < ENTITIES_COUNT; ++i) {
        entities[i].render(renderer, camera);
    }

    render_projectiles(renderer, camera);
}

void Game_State::entity_shoot(Entity_Index entity_index)
{
    assert(entity_index.unwrap < ENTITIES_COUNT);

    Entity *entity = &entities[entity_index.unwrap];

    if (entity->state != Entity_State::Alive) return;
    if (entity->cooldown_weapon > 0) return;

    const float PROJECTILE_SPEED = 1200.0f;

    const int ENTITY_COOLDOWN_WEAPON = 7;
    spawn_projectile(
        entity->pos,
        entity->gun_dir * PROJECTILE_SPEED,
        entity_index);
    entity->cooldown_weapon = ENTITY_COOLDOWN_WEAPON;
}

void Game_State::inplace_spawn_entity(Entity_Index index,
                                      Vec2f pos)
{
    const int ENTITY_TEXBOX_SIZE = 64;
    const int ENTITY_HITBOX_SIZE = ENTITY_TEXBOX_SIZE - 20;

    const Rectf texbox_local = {
        - (ENTITY_TEXBOX_SIZE / 2), - (ENTITY_TEXBOX_SIZE / 2),
        ENTITY_TEXBOX_SIZE, ENTITY_TEXBOX_SIZE
    };
    const Rectf hitbox_local = {
        - (ENTITY_HITBOX_SIZE / 2), - (ENTITY_HITBOX_SIZE / 2),
        ENTITY_HITBOX_SIZE, ENTITY_HITBOX_SIZE
    };

    const float POOF_DURATION = 0.2f;

    memset(entities + index.unwrap, 0, sizeof(Entity));
    entities[index.unwrap].state = Entity_State::Alive;
    entities[index.unwrap].alive_state = Alive_State::Idle;
    entities[index.unwrap].texbox_local = texbox_local;
    entities[index.unwrap].hitbox_local = hitbox_local;
    entities[index.unwrap].pos = pos;
    entities[index.unwrap].gun_dir = vec2(1.0f, 0.0f);
    entities[index.unwrap].poof.duration = POOF_DURATION;

    entities[index.unwrap].walking = entity_walking_animat;
    entities[index.unwrap].idle = entity_idle_animat;

    entities[index.unwrap].prepare_for_jump_animat.begin = 0.0f;
    entities[index.unwrap].prepare_for_jump_animat.end = 0.2f;
    entities[index.unwrap].prepare_for_jump_animat.duration = 0.2f;

    entities[index.unwrap].jump_animat.rubber_animats[0].begin = 0.2f;
    entities[index.unwrap].jump_animat.rubber_animats[0].end = -0.2f;
    entities[index.unwrap].jump_animat.rubber_animats[0].duration = 0.1f;

    entities[index.unwrap].jump_animat.rubber_animats[1].begin = -0.2f;
    entities[index.unwrap].jump_animat.rubber_animats[1].end = 0.0f;
    entities[index.unwrap].jump_animat.rubber_animats[1].duration = 0.2f;

    entities[index.unwrap].jump_samples[0] = entity_jump_sample1;
    entities[index.unwrap].jump_samples[1] = entity_jump_sample2;
}

void Game_State::reset_entities()
{
    static_assert(ROOM_ROW_COUNT > 0);
    inplace_spawn_entity({PLAYER_ENTITY_INDEX},
                         room_row[0].center());

    for (size_t i = 0; i < ROOM_ROW_COUNT - 1; ++i) {
        inplace_spawn_entity({ENEMY_ENTITY_INDEX_OFFSET + i},
                             room_row[i + 1].center());
    }
}

void Game_State::spawn_projectile(Vec2f pos, Vec2f vel, Entity_Index shooter)
{
    const float PROJECTILE_LIFETIME = 5.0f;
    for (size_t i = 0; i < PROJECTILES_COUNT; ++i) {
        if (projectiles[i].state == Projectile_State::Ded) {
            projectiles[i].state = Projectile_State::Active;
            projectiles[i].pos = pos;
            projectiles[i].vel = vel;
            projectiles[i].shooter = shooter;
            projectiles[i].lifetime = PROJECTILE_LIFETIME;
            projectiles[i].active_animat = projectile_active_animat;
            projectiles[i].poof_animat = projectile_poof_animat;
            return;
        }
    }
}

void Game_State::render_debug_overlay(SDL_Renderer *renderer)
{
    sec(SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255));

    const float COLLISION_PROBE_SIZE = 10.0f;
    const auto collision_probe_rect = rect(
        camera.to_screen(collision_probe - COLLISION_PROBE_SIZE),
        COLLISION_PROBE_SIZE * 2, COLLISION_PROBE_SIZE * 2);
    {
        auto rect = rectf_for_sdl(collision_probe_rect);
        sec(SDL_RenderFillRect(renderer, &rect));
    }

    auto index = room_index_at(debug_mouse_position);
    auto room_boundary_screen =
        camera.to_screen(ROOM_BOUNDARY + vec2((float) index.unwrap * ROOM_BOUNDARY.w, 1.0f));
    {
        auto rect = rectf_for_sdl(room_boundary_screen);
        sec(SDL_RenderDrawRect(renderer, &rect));
    }

    const float PADDING = 10.0f;
    // TODO(#38): FPS display is broken
    const SDL_Color GENERAL_DEBUG_COLOR = {255, 0, 0, 255};
    displayf(renderer, debug_font,
             GENERAL_DEBUG_COLOR, vec2(PADDING, PADDING),
             "FPS: %d", 60);
    displayf(renderer, debug_font,
             GENERAL_DEBUG_COLOR, vec2(PADDING, 50 + PADDING),
             "Mouse Position: (%.4f, %.4f)",
             debug_mouse_position.x,
             debug_mouse_position.y);
    displayf(renderer, debug_font,
             GENERAL_DEBUG_COLOR, vec2(PADDING, 2 * 50 + PADDING),
             "Collision Probe: (%.4f, %.4f)",
             collision_probe.x,
             collision_probe.y);
    displayf(renderer, debug_font,
             GENERAL_DEBUG_COLOR, vec2(PADDING, 3 * 50 + PADDING),
             "Projectiles: %d",
             count_alive_projectiles());
    displayf(renderer, debug_font,
             GENERAL_DEBUG_COLOR, vec2(PADDING, 4 * 50 + PADDING),
             "Player position: (%.4f, %.4f)",
             entities[PLAYER_ENTITY_INDEX].pos.x,
             entities[PLAYER_ENTITY_INDEX].pos.y);
    displayf(renderer, debug_font,
             GENERAL_DEBUG_COLOR, vec2(PADDING, 5 * 50 + PADDING),
             "Player velocity: (%.4f, %.4f)",
             entities[PLAYER_ENTITY_INDEX].vel.x,
             entities[PLAYER_ENTITY_INDEX].vel.y);

    const auto minimap_position = vec2(PADDING, 6 * 50 + PADDING);
    render_room_row_minimap(renderer, minimap_position);
    render_entity_on_minimap(
        renderer,
        vec2((float) minimap_position.x, (float) minimap_position.y),
        entities[PLAYER_ENTITY_INDEX].pos);


    if (tracking_projectile.has_value) {
        auto projectile = projectiles[tracking_projectile.unwrap.unwrap];
        const float SECOND_COLUMN_OFFSET = 700.0f;
        const SDL_Color TRACKING_DEBUG_COLOR = {255, 255, 0, 255};
        displayf(renderer, debug_font,
                 TRACKING_DEBUG_COLOR, vec2(PADDING + SECOND_COLUMN_OFFSET, PADDING),
                 "State: %s", projectile_state_as_cstr(projectile.state));
        displayf(renderer, debug_font,
                 TRACKING_DEBUG_COLOR, vec2(PADDING + SECOND_COLUMN_OFFSET, 50 + PADDING),
                 "Position: (%.4f, %.4f)",
                 projectile.pos.x, projectile.pos.y);
        displayf(renderer, debug_font,
                 TRACKING_DEBUG_COLOR, vec2(PADDING + SECOND_COLUMN_OFFSET, 2 * 50 + PADDING),
                 "Velocity: (%.4f, %.4f)",
                 projectile.vel.x, projectile.vel.y);
        displayf(renderer, debug_font,
                 TRACKING_DEBUG_COLOR, vec2(PADDING + SECOND_COLUMN_OFFSET, 3 * 50 + PADDING),
                 "Shooter Index: %d",
                 projectile.shooter.unwrap);
    }

    for (size_t i = 0; i < ENTITIES_COUNT; ++i) {
        if (entities[i].state == Entity_State::Ded) continue;

        sec(SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255));
        auto dstrect = rectf_for_sdl(camera.to_screen(entities[i].texbox_world()));
        sec(SDL_RenderDrawRect(renderer, &dstrect));

        sec(SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255));
        auto hitbox = rectf_for_sdl(camera.to_screen(entities[i].hitbox_world()));
        sec(SDL_RenderDrawRect(renderer, &hitbox));
    }

    if (tracking_projectile.has_value) {
        sec(SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255));
        auto hitbox = rectf_for_sdl(
            camera.to_screen(hitbox_of_projectile(tracking_projectile.unwrap)));
        sec(SDL_RenderDrawRect(renderer, &hitbox));
    }

    auto projectile_index = projectile_at_position(debug_mouse_position);
    if (projectile_index.has_value) {
        sec(SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255));
        auto hitbox = rectf_for_sdl(
            camera.to_screen(hitbox_of_projectile(projectile_index.unwrap)));
        sec(SDL_RenderDrawRect(renderer, &hitbox));
        return;
    }

    sec(SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255));
    const Rectf tile_rect = {
        floorf(debug_mouse_position.x / TILE_SIZE) * TILE_SIZE,
        floorf(debug_mouse_position.y / TILE_SIZE) * TILE_SIZE,
        TILE_SIZE,
        TILE_SIZE
    };
    {
        auto rect = rectf_for_sdl(camera.to_screen(tile_rect));
        sec(SDL_RenderDrawRect(renderer, &rect));
    }
}

int Game_State::count_alive_projectiles(void)
{
    int res = 0;
    for (size_t i = 0; i < PROJECTILES_COUNT; ++i) {
        if (projectiles[i].state != Projectile_State::Ded) ++res;
    }
    return res;
}

void Game_State::render_projectiles(SDL_Renderer *renderer, Camera camera)
{
    for (size_t i = 0; i < PROJECTILES_COUNT; ++i) {
        switch (projectiles[i].state) {
        case Projectile_State::Active: {
            render_animat(renderer,
                          projectiles[i].active_animat,
                          camera.to_screen(projectiles[i].pos));
        } break;

        case Projectile_State::Poof: {
            render_animat(renderer,
                          projectiles[i].poof_animat,
                          camera.to_screen(projectiles[i].pos));
        } break;

        case Projectile_State::Ded: {} break;
        }
    }
}

void Game_State::update_projectiles(float dt)
{
    for (size_t i = 0; i < PROJECTILES_COUNT; ++i) {
        switch (projectiles[i].state) {
        case Projectile_State::Active: {
            update_animat(&projectiles[i].active_animat, dt);
            projectiles[i].pos += projectiles[i].vel * dt;

            int room_current = (int) floorf(projectiles[i].pos.x / ROOM_BOUNDARY.w);
            if (0 <= room_current && room_current < (int) ROOM_ROW_COUNT) {
                if (!room_row[room_current].is_tile_at_abs_p_empty(projectiles[i].pos)) {
                    projectiles[i].state = Projectile_State::Poof;
                    projectiles[i].poof_animat.frame_current = 0;
                }
            }

            projectiles[i].lifetime -= dt;

            if (projectiles[i].lifetime <= 0.0f) {
                projectiles[i].state = Projectile_State::Poof;
                projectiles[i].poof_animat.frame_current = 0;
            }
        } break;

        case Projectile_State::Poof: {
            update_animat(&projectiles[i].poof_animat, dt);
            if (projectiles[i].poof_animat.frame_current ==
                (projectiles[i].poof_animat.frame_count - 1)) {
                projectiles[i].state = Projectile_State::Ded;
            }
        } break;

        case Projectile_State::Ded: {} break;
        }
    }

}

const float PROJECTILE_TRACKING_PADDING = 50.0f;

Rectf Game_State::hitbox_of_projectile(Projectile_Index index)
{
    assert(index.unwrap < PROJECTILES_COUNT);
    return Rectf {
        projectiles[index.unwrap].pos.x - PROJECTILE_TRACKING_PADDING * 0.5f,
            projectiles[index.unwrap].pos.y - PROJECTILE_TRACKING_PADDING * 0.5f,
            PROJECTILE_TRACKING_PADDING,
            PROJECTILE_TRACKING_PADDING
            };
}

Maybe<Projectile_Index> Game_State::projectile_at_position(Vec2f position)
{
    for (size_t i = 0; i < PROJECTILES_COUNT; ++i) {
        if (projectiles[i].state == Projectile_State::Ded) continue;

        Rectf hitbox = hitbox_of_projectile({i});
        if (rect_contains_vec2(hitbox, position)) {
            return {true, {i}};
        }
    }

    return {};
}
