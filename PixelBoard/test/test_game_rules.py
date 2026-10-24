import pathlib
import shutil
import subprocess
import tempfile
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
               step: int = 14, maximum: int = 286) -> int:
    if every <= 0 or hits <= 0 or hits % every:
        return min(current, maximum)
    return min(current + step, maximum)


def all_snake_players_lost(p1_alive: bool, p2_joined: bool,
                           p2_alive: bool) -> bool:
    return not p1_alive and (not p2_joined or not p2_alive)


class GameRuleTests(unittest.TestCase):
    def test_offline_i2c_wait_feeds_real_watchdog_code(self):
        compiler = shutil.which("g++")
        if compiler is None:
            self.skipTest("Host C++ compiler is not installed")
        with tempfile.TemporaryDirectory(prefix="pixelboard-watchdog-") as output:
            binary = pathlib.Path(output) / "runtime_health_test.exe"
            build = subprocess.run([
                compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                "-I" + str(ROOT / "test/host"),
                "-I" + str(ROOT / "lib/RuntimeHealth"),
                str(ROOT / "test/host/runtime_health_test.cpp"),
                str(ROOT / "lib/RuntimeHealth/RuntimeHealth.cpp"),
                "-o", str(binary),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("watchdog feed gap <= 250ms", run.stdout)

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
        speed = 108
        self.assertEqual(pong_speed(speed, 1), 108)
        speed = pong_speed(speed, 2)
        self.assertEqual(speed, 122)
        for hit in range(3, 80):
            speed = pong_speed(speed, hit)
        self.assertEqual(speed, 286)

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
        self.assertIn("delayWithRuntimeWatchdog(connected || validPackets > 0 ? 15 : 10000)", i2c_handler)
        self.assertNotIn("vTaskDelay(connected", i2c_handler)
        self.assertIn("I2C-Joystick nicht verbunden", i2c_handler)
        self.assertNotIn("Wire.end()", i2c_handler)
        self.assertIn("LittleFS.begin(false)", storage)
        self.assertIn("LittleFS.begin(true)", storage)
        self.assertIn("LittleFS.end()", storage)

    def test_runtime_settings_navigation_and_audio_optimizations_are_wired(self):
        config = (ROOT / "lib/Config/Config.cpp").read_text(encoding="utf-8")
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        sound = (ROOT / "lib/SoundUtils/SoundUtils.cpp").read_text(encoding="utf-8")

        self.assertNotIn('deviceConfigPath', config)
        self.assertNotIn('markDeviceSettingsDirty', config)
        self.assertNotIn('localStorage', config)
        self.assertIn('currentCity = "Innsbruck"', config)
        self.assertIn("requestAppTaskSwitch(11)", config)
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

    def test_shooter_dashboard_uses_bounded_memory_and_polling(self):
        config = (ROOT / "lib/Config/Config.cpp").read_text(encoding="utf-8")
        shooter = config.split("void handleShooterPage()", 1)[1].split(
            "void handleShooterState()", 1
        )[0]
        web = (ROOT / "lib/TaskRaumschiff/RaumschiffWeb.cpp").read_text(encoding="utf-8")
        self.assertIn("static const char shooterHtml[] PROGMEM", shooter)
        self.assertIn("server.send_P", shooter)
        self.assertIn("BoundedWebServer server(80)", config)
        self.assertNotIn("String html", shooter)
        self.assertIn("setInterval(state,1200)", shooter)
        self.assertIn("setInterval(ping,3000)", shooter)
        self.assertNotIn('"top"', web)
        self.assertNotIn("getTopScores", web)
        self.assertNotIn("p1name", shooter)
        self.assertNotIn("p2mode", shooter)

    def test_fixed_players_replace_login_and_user_database(self):
        header = (ROOT / "lib/Config/Config.h").read_text(encoding="utf-8")
        config = (ROOT / "lib/Config/Config.cpp").read_text(encoding="utf-8")
        pong = (ROOT / "lib/TaskPong/TaskPong.cpp").read_text(encoding="utf-8")
        pacman = (ROOT / "lib/TaskPacman/TaskPacman.cpp").read_text(encoding="utf-8")

        self.assertIn('PRIMARY_PLAYER_NAME = "NothinBL"', header)
        self.assertIn('SECONDARY_PLAYER_NAME = "Guest"', header)
        self.assertNotIn('server.on("/doLogin"', config)
        self.assertNotIn('server.on("/pong/login"', config)
        self.assertNotIn('server.on("/pacman/login"', config)
        self.assertIn("controls.primaryDoubleClick && !rightHuman", pong)
        self.assertIn("controls.secondaryDoubleClick && !leftHuman", pong)
        self.assertIn("controls.primaryDoubleClick && !playerHuman[0]", pacman)
        self.assertNotIn("pacmanGhostManual", pacman)

    def test_all_games_have_autoplay_and_removed_dashboards(self):
        config = (ROOT / "lib/Config/Config.cpp").read_text(encoding="utf-8")
        pong = (ROOT / "lib/TaskPong/TaskPong.cpp").read_text(encoding="utf-8")
        pacman = (ROOT / "lib/TaskPacman/TaskPacman.cpp").read_text(encoding="utf-8")
        mario = (ROOT / "lib/TaskMario/TaskMario.cpp").read_text(encoding="utf-8")
        tetris = (ROOT / "lib/TaskTetris/TaskTetris.cpp").read_text(encoding="utf-8")
        snake = (ROOT / "lib/TaskSnake/TaskSnake.cpp").read_text(encoding="utf-8")

        self.assertNotIn('server.on("/pong"', config)
        self.assertNotIn('server.on("/pacman"', config)
        self.assertNotIn('server.on("/mario"', config)
        self.assertIn("bool botsOnly = !leftHuman && !rightHuman", pong)
        self.assertIn("ballVxAbs + 16", pong)
        self.assertIn("choosePacmanBotDir", pacman)
        self.assertIn("firstDir", pacman)
        self.assertIn("readBotInput", mario)
        self.assertIn("planBotPlacement", tetris)
        self.assertIn("chooseBotDirection", snake)

    def test_main_menu_has_separate_controller_volume_controls(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")

        self.assertIn("adjustMenuChannelVolume(joystick1, true, 0)", main)
        self.assertIn("adjustMenuChannelVolume(joystick3, false, 1)", main)
        self.assertIn("toggleMenuChannelMute(true)", main)
        self.assertIn("toggleMenuChannelMute(false)", main)
        self.assertIn("drawMenuVolumeBar(0, getMusicVolume()", main)
        self.assertIn("drawMenuVolumeBar(2, getEffectsVolume()", main)
        self.assertIn("FastLED.setBrightness(255)", main)
        self.assertIn("FastLED.setMaxPowerInVoltsAndMilliamps(5, 2400)", main)

    def test_game_results_and_bot_pacing_are_bounded(self):
        mario = (ROOT / "lib/TaskMario/TaskMario.cpp").read_text(encoding="utf-8")
        pong = (ROOT / "lib/TaskPong/TaskPong.cpp").read_text(encoding="utf-8")
        tetris = (ROOT / "lib/TaskTetris/TaskTetris.cpp").read_text(encoding="utf-8")

        self.assertIn('won ? "WIN" : "LOSE"', mario)
        self.assertIn('drawCenteredWord("SCORE"', mario)
        self.assertIn("marioLives = 3;", mario.split("void startNextLevel()", 1)[1].split("}", 1)[0])
        self.assertIn("END_SCREEN_MS = 3500", mario)
        self.assertIn("pongBotMode == 1 ? 24", pong)
        self.assertIn("BOT_ACTION_MS = 150", tetris)
        self.assertIn("BOT_DROP_DELAY_MS = 700", tetris)
        self.assertIn("MAX_TETRIS_SCORE = 999999", tetris)
        self.assertIn("addScoreSafe", tetris)
        self.assertIn("didHardDrop = runBot(now)", tetris)
        self.assertIn("if (rotatePiece()) return false", tetris)
        self.assertIn("renderPlaying(false)", tetris)

    def test_compact_web_and_runtime_bot_modes(self):
        config = (ROOT / "lib/Config/Config.cpp").read_text(encoding="utf-8")
        pong = (ROOT / "lib/TaskPong/TaskPong.cpp").read_text(encoding="utf-8")
        tetris = (ROOT / "lib/TaskTetris/TaskTetris.cpp").read_text(encoding="utf-8")

        for removed_text in (
            "Spiele laufen automatisch",
            "Spieler-Kills laden Energie",
            "Schaden steigt",
            "Aenderungen gelten",
            "Netzwerk:",
            "Mario Dashboard",
            "Pacman Dashboard",
            "Pong Dashboard",
        ):
            self.assertNotIn(removed_text, config)

        self.assertIn("pongBotMode", config)
        self.assertIn('action=\'/updatePongMode\'', config)
        self.assertIn('"Wie jetzt"', config)
        self.assertIn('"Super"', config)
        self.assertNotIn('"Plausibel"', config)
        self.assertNotIn('pongBotMode, "Normal"', config)
        self.assertIn("pongBotMode == 1 ? 24", pong)
        self.assertIn("int aimError = 0", pong)
        self.assertIn("pongBotMode == 1 ? 32 : INPUT_STEP_MS", pong)
        self.assertIn("FAST_BOT_ACTION_MS = 75", tetris)
        self.assertIn("tetrisFastBotMode ? FAST_BOT_ACTION_MS : BOT_ACTION_MS", tetris)

    def test_pong_bots_move_tactically_and_aim_away(self):
        pong = (ROOT / "lib/TaskPong/TaskPong.cpp").read_text(encoding="utf-8")

        self.assertIn("INITIAL_VX = 108", pong)
        self.assertIn("MAX_VX = 286", pong)
        self.assertIn("ballVxAbs + 16", pong)
        self.assertIn("auto botTargetCenter", pong)
        self.assertIn("impact - aimAway + aimError", pong)
        self.assertIn("nextImpact + ballPixelY", pong)
        self.assertNotIn(": HEIGHT / 2;", pong.split("void applyInput", 1)[1].split("void drawNumber2", 1)[0])

    def test_pong_preview_uses_real_bounces_and_guaranteed_paddle_hits(self):
        pong = (ROOT / "lib/TaskPong/TaskPong.cpp").read_text(encoding="utf-8")
        preview = pong.split("void renderMenu()", 1)[1].split("void renderCountdown", 1)[0]

        self.assertIn("predictedPreviewImpact", preview)
        self.assertIn("previewBallX <= 3 * SCALE", preview)
        self.assertIn("previewBallX >= 28 * SCALE", preview)
        self.assertIn("previewBallVx = abs(previewBallVx)", preview)
        self.assertIn("previewBallVx = -abs(previewBallVx)", preview)
        self.assertNotIn("sin(", preview)

    def test_tetris_four_digit_layout_and_snake_bot_reservations(self):
        tetris = (ROOT / "lib/TaskTetris/TaskTetris.cpp").read_text(encoding="utf-8")
        snake = (ROOT / "lib/TaskSnake/TaskSnake.cpp").read_text(encoding="utf-8")

        self.assertIn("HOLD_Y = 5", tetris)
        self.assertIn("NEXT_Y = 5", tetris)
        self.assertIn("SCORE_X = 0", tetris)
        self.assertIn("value > 9999", tetris)
        self.assertIn("score % 10000UL", tetris)
        self.assertIn("char buf[5]", tetris)
        self.assertIn("Point reservedNext, bool hasReservation", snake)
        self.assertIn("pointEquals(next, reservedNext)", snake)
        self.assertIn("safeExits", snake)
        self.assertIn("Point plannedNext[2]", snake)
        self.assertIn("chooseBotDirection(1, plannedNext[0]", snake)

    def test_snake_controllers_claim_bots_before_a_second_human_joins(self):
        config_h = (ROOT / "lib/Config/Config.h").read_text(encoding="utf-8")
        config = (ROOT / "lib/Config/Config.cpp").read_text(encoding="utf-8")
        snake = (ROOT / "lib/TaskSnake/TaskSnake.cpp").read_text(encoding="utf-8")

        self.assertIn("extern uint8_t snakeBotCount", config_h)
        self.assertIn("uint8_t snakeBotCount = 2", config)
        self.assertIn("<select name='bots'>", config)
        self.assertIn('server.hasArg("bots")', config)
        self.assertIn("snakeBotCount >= 2 || playerHuman[1]", snake)
        self.assertIn("auto claimBot", snake)
        self.assertIn("claimBot(0, 0)", snake)
        self.assertIn("claimBot(1, 1)", snake)
        self.assertIn("humanController[player] == 0", snake)
        self.assertIn("auto joinSecondHuman", snake)
        self.assertIn("!claimBot(0, 0)) joinSecondHuman(0)", snake)
        self.assertIn("!claimBot(1, 1)) joinSecondHuman(1)", snake)

    def test_takeovers_wait_for_neutral_input_in_every_autoplay_game(self):
        pong = (ROOT / "lib/TaskPong/TaskPong.cpp").read_text(encoding="utf-8")
        snake = (ROOT / "lib/TaskSnake/TaskSnake.cpp").read_text(encoding="utf-8")
        tetris = (ROOT / "lib/TaskTetris/TaskTetris.cpp").read_text(encoding="utf-8")
        mario = (ROOT / "lib/TaskMario/TaskMario.cpp").read_text(encoding="utf-8")
        shooter = (ROOT / "lib/TaskRaumschiff/RaumschiffGame.cpp").read_text(encoding="utf-8")

        self.assertIn("rightHumanInputArmed = false", pong)
        self.assertIn("pongStickNeutral", pong)
        self.assertIn("humanInputArmed[target] = false", snake)
        self.assertIn("!snakeJoystickMoved(stick)", snake)
        self.assertIn("humanInputArmed = false", tetris)
        self.assertIn("!joystick1.isPressed()", tetris)
        self.assertIn("humanInputArmed = false", mario)
        self.assertIn("mario.vx = 0", mario)
        self.assertIn("humanInputArmed[RAUMSCHIFF_MAX_PLAYERS]", shooter)
        self.assertIn("humanInputArmed[0] ? readJoystickInput", shooter)

    def test_snake_route_and_pacman_icon_previews_stay_readable(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        snake = (ROOT / "lib/TaskSnake/TaskSnake.cpp").read_text(encoding="utf-8")
        pacman = (ROOT / "lib/TaskPacman/TaskPacman.cpp").read_text(encoding="utf-8")

        self.assertIn("renderSnakeMenuPreview()", main)
        self.assertIn("snakePreviewRoutePoint", snake)
        self.assertIn("Point segment = snakePreviewRoutePoint(headStep - i)", snake)
        lobby = pacman.split("void renderLobby", 1)[1].split("void renderCountdown", 1)[0]
        self.assertIn("mouthOpen", lobby)
        self.assertIn("CRGB::Yellow", lobby)
        self.assertIn("CRGB::Cyan", lobby)
        self.assertNotIn("routePoint", lobby)

    def test_web_responses_do_not_fragment_heap_or_trigger_task_watchdog(self):
        config = (ROOT / "lib/Config/Config.cpp").read_text(encoding="utf-8")

        dashboard = config.split("static void sendDashboardHtml", 1)[1].split(
            "static String soundStateJson", 1
        )[0]
        self.assertIn('server.send(200, "text/html; charset=utf-8", html)', dashboard)
        self.assertNotIn("globalSoundControl", dashboard)
        self.assertNotIn("sendContent", dashboard)
        self.assertNotIn("CONTENT_LENGTH_UNKNOWN", dashboard)
        self.assertNotIn("substring", dashboard)

        bounded_send = config.split("class BoundedWebServer", 1)[1].split(
            "BoundedWebServer server(80)", 1
        )[0]
        self.assertIn("_currentClientWrite(const char* data, size_t length) override", bounded_send)
        self.assertIn("_currentClientWrite_P(PGM_P data, size_t length) override", bounded_send)
        self.assertIn("size_t streamFile(File& file", bounded_send)
        self.assertIn("MSG_DONTWAIT", bounded_send)
        self.assertIn("const unsigned long maxStallMs = 1500", bounded_send)
        self.assertIn("const unsigned long maxResponseMs = 5000", bounded_send)
        self.assertIn("const size_t sliceSize = 384", bounded_send)
        self.assertIn("_currentClient.stop()", bounded_send)
        self.assertIn("abortResponse(ETIMEDOUT)", bounded_send)

        web_task = config.split("void taskWebServerHandler", 1)[1].split("#if 0", 1)[0]
        self.assertNotIn("registerCurrentTaskWithWatchdog", web_task)
        self.assertNotIn("feedRuntimeWatchdog", web_task)
        self.assertIn("server.handleClient()", web_task)

        save_wifi = config.split("void saveWifiCredentials", 1)[1].split(
            "void loadWifiCredentials", 1
        )[0]
        self.assertIn("wifiReconnectRequested = true", save_wifi)
        self.assertNotIn("beginWifiConnection()", save_wifi)

        connected_wifi = config.split("void maintainWifiConnection", 1)[1].split(
            "void saveWifiCredentials", 1
        )[0]
        self.assertIn("if (WiFi.status() == WL_CONNECTED)", connected_wifi)
        self.assertIn("startCaptivePortal();", connected_wifi)
        self.assertNotIn("stopCaptivePortal();", connected_wifi)
        self.assertNotIn("accessPointSyncedToStation", config)
        self.assertIn("WiFi.setTxPower(WIFI_POWER_19_5dBm)", config)
        self.assertIn("WiFi.setSleep(false)", config)

        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        self.assertIn('createTaskChecked(taskWebServerHandler, "Web", 8192', main)
        setup = main.split("void setup()", 1)[1].split("FastLED.addLeds", 1)[0]
        self.assertIn("if (hasWifiCredentials())", setup)
        self.assertIn("beginWifiConnection();", setup)
        self.assertIn("else\n        startCaptivePortal();", setup)
        self.assertNotIn("startCaptivePortal();\n    if (hasWifiCredentials())", setup)
        self.assertIn("delayWithRuntimeWatchdog(connected || validPackets > 0 ? 15 : 10000)", main)

    def test_music_visualizer_and_mario_preview_are_action_scenes(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        mario = (ROOT / "lib/TaskMario/TaskMario.cpp").read_text(encoding="utf-8")

        self.assertIn("static void renderPhonkVisualizer", main)
        self.assertGreaterEqual(main.count("renderPhonkVisualizer(jetzt)"), 2)
        self.assertIn("const uint16_t beatPhase", main)
        self.assertIn("Schnelle diagonale Neonspuren", main)

        lobby = mario.split("void renderLobby()", 1)[1].split(
            "void drawCenteredWord", 1
        )[0]
        self.assertIn("jumpHeight", lobby)
        self.assertIn("coinCollected", lobby)
        self.assertIn("goombaX", lobby)
        self.assertNotIn("renderWorld()", lobby)

    def test_joysticks_do_not_read_adc_before_arduino_setup(self):
        joystick = (ROOT / "lib/Joystick/Joystick.cpp").read_text(encoding="utf-8")
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")

        constructor = joystick.split("Joystick::Joystick(int pinX", 1)[1].split(
            "Joystick::Joystick()", 1
        )[0]
        self.assertNotIn("kalibrieren();", constructor)
        self.assertIn("xMitte = 2048", constructor)
        self.assertIn("joystick1.kalibrieren();", main)
        self.assertIn("joystick2.kalibrieren();", main)

    def test_tetris_bot_scores_cleared_board_and_avoids_buried_holes(self):
        tetris = (ROOT / "lib/TaskTetris/TaskTetris.cpp").read_text(encoding="utf-8")

        planner = tetris.split("void planBotPlacement()", 1)[1].split("int8_t randomPiece()", 1)[0]
        self.assertIn("memcpy(simulated[yy], simulated[yy - 1], FIELD_W)", planner)
        self.assertIn("coveredHoleDepth", planner)
        self.assertIn("wells", planner)
        self.assertIn("landingHeight", planner)
        self.assertIn("holes * 6500L", planner)

    def test_pacman_takeover_stops_bot_motion_until_stick_is_released(self):
        pacman = (ROOT / "lib/TaskPacman/TaskPacman.cpp").read_text(encoding="utf-8")

        self.assertIn("humanInputArmed[PLAYER_COUNT]", pacman)
        self.assertIn("void takeOverPlayer(uint8_t playerIndex)", pacman)
        self.assertIn("players[playerIndex].dir = makeVec(0, 0)", pacman)
        self.assertIn("if (stickIsNeutral(stick)) humanInputArmed[playerIndex] = true", pacman)
        self.assertIn("if (human) {\n            player.dir = makeVec(0, 0);", pacman)
        self.assertIn("takeOverPlayer(0)", pacman)
        self.assertIn("takeOverPlayer(1)", pacman)

    def test_standby_text_mode_is_runtime_only_and_animated(self):
        config = (ROOT / "lib/Config/Config.cpp").read_text(encoding="utf-8")
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        standby = (ROOT / "lib/TaskStandby/TaskStandby.cpp").read_text(encoding="utf-8")

        self.assertIn("APP_COUNT = 12", main)
        self.assertIn("renderStandbyMenuPreview()", main)
        self.assertIn('createTaskChecked(taskStandbyHandler, "Standby"', main)
        self.assertIn("standbyTextSnapshot()", config)
        self.assertIn("setStandbyText(server.arg(\"text\"))", config)
        self.assertIn("requestAppTaskSwitch(11)", config)
        self.assertIn("maxlength='32'", config)
        self.assertNotIn("standbyText", config.split("#if 0", 1)[1])
        self.assertIn("EFFECT_DURATION_MS = 7000", standby)
        self.assertIn("drawBackground(now, effect)", standby)
        self.assertIn("textWidth <= WIDTH", standby)
        self.assertIn("now / 85", standby)

    def test_menu_long_click_advances_from_every_task_once(self):
        main = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        button = (ROOT / "lib/Joystick/Taster-v2.cpp").read_text(encoding="utf-8")
        joystick = (ROOT / "lib/Joystick/Joystick.cpp").read_text(encoding="utf-8")
        app_clicks = main.split("struct ClickEvent readAppClicks()", 1)[1].split(
            "// ==========================================", 1
        )[0]

        self.assertIn("return readAndClearClicks();", app_clicks)
        self.assertNotIn("aktiverTask != 8", app_clicks)
        self.assertIn("else if (clicks.langKlick > 0)", main)
        self.assertNotIn("clicks.langKlick > 0 && aktiverTask != 8", main)
        self.assertIn("klickDauer >= langKlickZeit && !langKlickErkannt", button)
        self.assertIn("klickDauer >= langKlickZeit && !langKlickErkannt", joystick)

    def test_no_drift_or_persistent_game_settings_and_dht_autodetects(self):
        joystick_h = (ROOT / "lib/Joystick/Joystick.h").read_text(encoding="utf-8")
        joystick_cpp = (ROOT / "lib/Joystick/Joystick.cpp").read_text(encoding="utf-8")
        storage = (ROOT / "lib/ConfigStorage/ConfigStorage.cpp").read_text(encoding="utf-8")
        dht = (ROOT / "lib/TaskDHT/TaskDHT.cpp").read_text(encoding="utf-8")
        tetris = (ROOT / "lib/TaskTetris/TaskTetris.cpp").read_text(encoding="utf-8")

        self.assertNotIn("Drift", joystick_h)
        self.assertNotIn("Drift", joystick_cpp)
        self.assertIn('removeObsoleteRuntimeFile("/device.json")', storage)
        self.assertIn('removeObsoleteRuntimeFile("/config.json")', storage)
        self.assertIn("Automatischer Wechsel auf DHT", dht)
        self.assertIn("sensorReadyAfterMs", dht)
        self.assertNotIn("tetrisLastScore", tetris)
        self.assertNotIn("scoreSaved", tetris)


if __name__ == "__main__":
    unittest.main()
