extends Node

signal game_state_changed(new_state: GameState)
signal game_mode_changed(new_mode: GameMode)

enum GameState { MENU, LOADING, IN_GAME, PAUSED, GAME_OVER }
enum GameMode { TDM, GUN_GAME }

var current_state: GameState = GameState.MENU
var current_mode: GameMode = GameMode.TDM

func _ready() -> void:
	process_mode = PROCESS_MODE_ALWAYS

func set_game_state(new_state: GameState) -> void:
	if current_state != new_state:
		current_state = new_state
		game_state_changed.emit(new_state)

func set_game_mode(new_mode: GameMode) -> void:
	if current_mode != new_mode:
		current_mode = new_mode
		game_mode_changed.emit(new_mode)
