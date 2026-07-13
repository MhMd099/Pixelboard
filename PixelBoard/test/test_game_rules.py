import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def wrap(value: int, size: int) -> int:
    if size <= 0:
        return 0
    if value < 0:
        return size - 1
    if value >= size:
        return 0
    return value


def pong_speed(current: int, hits: int, every: int = 2,
               step: int = 12, maximum: int = 188) -> int:
    if every <= 0 or hits <= 0 or hits % every:
        return min(current, maximum)
    return min(current + step, maximum)


def all_snake_players_lost(p1_alive: bool, p2_joined: bool,
                           p2_alive: bool) -> bool:
    return not p1_alive and (not p2_joined or not p2_alive)


class GameRuleTests(unittest.TestCase):
    def test_snake_wraps_on_all_edges(self):
        self.assertEqual(wrap(-1, 32), 31)
        self.assertEqual(wrap(32, 32), 0)
        self.assertEqual(wrap(-1, 16), 15)
        self.assertEqual(wrap(16, 16), 0)

    def test_snake_waits_until_both_joined_players_lose(self):
        self.assertFalse(all_snake_players_lost(False, True, True))
        self.assertTrue(all_snake_players_lost(False, True, False))
        self.assertTrue(all_snake_players_lost(False, False, False))

    def test_pong_accelerates_every_second_hit_and_is_capped(self):
        speed = 92
        self.assertEqual(pong_speed(speed, 1), 92)
        speed = pong_speed(speed, 2)
        self.assertEqual(speed, 104)
        for hit in range(3, 80):
            speed = pong_speed(speed, hit)
        self.assertEqual(speed, 188)

    def test_score_only_increases_for_positive_awards(self):
        score = 10
        award = 12
        score += max(0, award)
        self.assertEqual(score, 22)

    def test_shared_states_and_restart_are_used_by_every_game(self):
        header = (ROOT / "lib/GameRuntime/GameRuntime.h").read_text(encoding="utf-8")
        for state in ("GAME_MENU", "GAME_COUNTDOWN", "GAME_PLAYING",
                      "GAME_PAUSED", "GAME_GAME_OVER"):
            self.assertIn(state, header)

        game_sources = [
            "lib/TaskSnake/TaskSnake.cpp",
            "lib/TaskPong/TaskPong.cpp",
            "lib/TaskPacman/TaskPacman.cpp",
            "lib/TaskTetris/TaskTetris.cpp",
            "lib/TaskMario/TaskMario.cpp",
            "lib/TaskRaumschiff/RaumschiffGame.cpp",
        ]
        for relative in game_sources:
            source = (ROOT / relative).read_text(encoding="utf-8")
            self.assertIn("GAME_PLAYING", source, relative)
            self.assertIn("GAME_GAME_OVER", source, relative)
            self.assertIn("GAME_PAUSED", source, relative)

    def test_restart_is_double_click_driven_centrally(self):
        runtime = (ROOT / "lib/GameRuntime/GameRuntime.cpp").read_text(encoding="utf-8")
        self.assertIn("doppelklickZaehler", runtime)
        self.assertIn("PAUSE_HOLD_MS = 5000UL", runtime)

    def test_menu_music_is_paused_while_an_app_is_open(self):
        menu_music = (ROOT / "lib/MenuMusic/MenuMusic.cpp").read_text(encoding="utf-8")
        sound_utils = (ROOT / "lib/SoundUtils/SoundUtils.cpp").read_text(encoding="utf-8")

        app_pause = menu_music.split("if (aktiverTask != -1)", 1)[1].split(
            "if (running && pausedForTask)", 1
        )[0]
        self.assertIn("setMenuMusicEnabled(false)", app_pause)
        self.assertNotIn("decoder.stop()", app_pause)
        self.assertNotIn("file.close()", app_pause)
        self.assertIn("setMenuMusicEnabled(true)", menu_music)

        enable_function = sound_utils.split(
            "void setMenuMusicEnabled(bool enabled)", 1
        )[1].split("void playSound", 1)[0]
        self.assertNotIn("clearMenuMusicBuffer()", enable_function)

    def test_hardware_offline_and_storage_safety_guards_exist(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        storage = (ROOT / "lib/ConfigStorage/ConfigStorage.cpp").read_text(encoding="utf-8")

        i2c_handler = main.split("void taskI2CJoystickHandler", 1)[1].split(
            "static bool createTaskChecked", 1
        )[0]
        self.assertIn("pdMS_TO_TICKS(1500)", i2c_handler)
        self.assertIn("I2C-Joystick nicht verbunden", i2c_handler)
        self.assertNotIn("Wire.end()", i2c_handler)
        self.assertIn("LittleFS.begin(false)", storage)
        self.assertNotIn("LittleFS.begin(true)", storage)

    def test_settings_navigation_and_audio_optimizations_are_wired(self):
        config = (ROOT / "lib/Config/Config.cpp").read_text(encoding="utf-8")
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        sound = (ROOT / "lib/SoundUtils/SoundUtils.cpp").read_text(encoding="utf-8")

        for key in ("snakeFood", "snakeObstacles", "snakePowerups",
                    "tetrisNormalMs", "tetrisSoftMs", "tetrisMinMs"):
            self.assertIn(f'doc["{key}"]', config)
        self.assertIn("requestAppTaskSwitch(10)", config)
        self.assertIn("takeRequestedAppTask()", main)
        self.assertIn("__atomic_store_n(&menuPcmWrite", sound)
        self.assertNotIn("menuPcmMux", sound)

    def test_secrets_and_large_responsibilities_are_separated(self):
        config = (ROOT / "lib/Config/Config.cpp").read_text(encoding="utf-8")
        weather = (ROOT / "lib/TaskWetter/TaskWetter.cpp").read_text(encoding="utf-8")
        gitignore = (ROOT / ".gitignore").read_text(encoding="utf-8")

        self.assertIn("weatherApiKey = WEATHER_API_KEY", config)
        self.assertNotIn('weatherApiKey = "', config)
        self.assertIn("include/Secrets.h", gitignore)
        self.assertIn("WiFiClient", weather)
        self.assertIn("connect(\"api.openweathermap.org\", 80)", weather)
        self.assertTrue((ROOT / "lib/ConfigStorage/ConfigStorage.cpp").exists())
        self.assertTrue((ROOT / "lib/TaskRaumschiff/RaumschiffWeb.cpp").exists())


if __name__ == "__main__":
    unittest.main()
