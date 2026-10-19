PYTHON ?= python3
# Инструменты разработки живут в .venv (создан с --system-site-packages),
# потому что дистрибутивный Python запрещает ставить пакеты глобально (PEP 668).
VENV := .venv
VENV_PY := $(VENV)/bin/python

PREFIX ?= $(HOME)/.local

# Переводы: .ts правятся в Qt Linguist, .qm собирается lrelease и коммитится,
# чтобы установка не требовала инструментов Qt. lupdate нужен только при
# добавлении новых строк и берётся из pyside6-tools — системный lupdate
# из qt6-l10n-tools не понимает Python.
# Утилиты Qt лежат вне PATH (/usr/lib/qt6/bin), поэтому ищем их явно.
LRELEASE ?= $(shell command -v lrelease 2>/dev/null || echo /usr/lib/qt6/bin/lrelease)
LUPDATE ?= $(shell command -v pyside6-lupdate 2>/dev/null || echo /usr/lib/qt6/bin/pyside6-lupdate)
# Компилируются оба языка, а сканируется только русский: английский .ts ведётся
# руками и содержит одни формы числа — lupdate залил бы в него весь каталог.
LANGUAGES := ru en
SCANNED_LANGUAGES := ru

# C++-версия собирается CMake в отдельном каталоге: build/ занят упаковкой .deb.
BUILD_DIR := build/cmake
CLANG_FORMAT := $(VENV)/bin/clang-format
CXX_SOURCES = $(shell find src tests -name '*.cpp' -o -name '*.hpp')

.PHONY: help run probe deps deps-i18n deps-cpp devenv check lint shellcheck format typecheck test coverage icons i18n-update i18n-release install uninstall deb clean cpp-build cpp-test cpp-format cpp-lint

help:
	@echo "make deps      — установить системные зависимости (нужен sudo)"
	@echo "make deps-i18n — инструменты для работы над переводами (нужен sudo)"
	@echo "make devenv    — создать .venv с инструментами разработки"
	@echo "make check     — линтер, скрипты, типы и тесты разом"
	@echo "make run       — запустить приложение"
	@echo "make probe     — диагностика метаданных живого потока"
	@echo "make lint      — ruff: правила и форматирование"
	@echo "make shellcheck — shellcheck для скриптов установки"
	@echo "make typecheck — mypy"
	@echo "make format    — отформатировать код и починить, что чинится само"
	@echo "make test      — pytest"
	@echo "make coverage  — pytest с измерением покрытия"
	@echo "make i18n-update  — обновить .ts новыми строками из кода"
	@echo "make i18n-release — собрать .qm из .ts"
	@echo "make install   — установить в $(PREFIX)"
	@echo "make uninstall — удалить из $(PREFIX)"
	@echo "make icons     — перерисовать PNG из SVG-иконки"
	@echo "make deb       — собрать .deb в dist/"
	@echo "make deps-cpp  — зависимости сборки C++-версии (нужен sudo)"
	@echo "make cpp-build — собрать C++-версию в $(BUILD_DIR)"
	@echo "make cpp-test  — собрать и прогнать тесты C++-версии"
	@echo "make cpp-format — отформатировать C++ (clang-format)"
	@echo "make cpp-lint  — проверить форматирование C++"

devenv:
	$(PYTHON) -m venv --system-site-packages $(VENV)
	$(VENV)/bin/pip install --quiet --requirement requirements-dev.txt

deps-cpp:
	sudo apt install -y cmake ninja-build g++ pkg-config qt6-base-dev libmpv-dev catch2

deps:
	sudo apt install -y libmpv2 python3-mpv \
		python3-pyside6.qtcore python3-pyside6.qtgui python3-pyside6.qtwidgets \
		python3-pyside6.qtnetwork python3-pyside6.qtdbus \
		qt6-svg qt6-translations-l10n

# Только для работы над переводами: lupdate с поддержкой Python.
deps-i18n:
	sudo apt install -y pyside6-tools qt6-l10n-tools qt6-tools

run:
	$(PYTHON) -m radio_player

probe:
	$(PYTHON) scripts/probe_metadata.py $(URL)

check: lint shellcheck typecheck test

lint: $(VENV_PY)
	$(VENV)/bin/ruff check .
	$(VENV)/bin/ruff format --check .

shellcheck: $(VENV_PY)
	$(VENV)/bin/shellcheck scripts/*.sh

format: $(VENV_PY)
	$(VENV)/bin/ruff format .
	$(VENV)/bin/ruff check --fix .

typecheck: $(VENV_PY)
	$(VENV)/bin/mypy

test: $(VENV_PY)
	$(VENV_PY) -m pytest

coverage: $(VENV_PY)
	$(VENV_PY) -m pytest --cov --cov-report=term

$(VENV_PY):
	$(MAKE) devenv

i18n-update:
	$(LUPDATE) -no-obsolete $$(find radio_player -name '*.py') \
		$(foreach lang,$(SCANNED_LANGUAGES),-ts translations/radio_player_$(lang).ts)

i18n-release:
	@mkdir -p radio_player/resources/i18n
	$(foreach lang,$(LANGUAGES),$(LRELEASE) translations/radio_player_$(lang).ts \
		-qm radio_player/resources/i18n/radio_player_$(lang).qm;)

install:
	PREFIX=$(PREFIX) sh scripts/install.sh

uninstall:
	PREFIX=$(PREFIX) sh scripts/uninstall.sh

icons:
	$(PYTHON) scripts/render_icons.py

deb:
	sh scripts/build_deb.sh

cpp-build:
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR)

cpp-test: cpp-build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

cpp-format: $(VENV_PY)
	$(CLANG_FORMAT) -i $(CXX_SOURCES)

cpp-lint: $(VENV_PY)
	$(CLANG_FORMAT) --dry-run --Werror $(CXX_SOURCES)

clean:
	find . -name __pycache__ -type d -prune -exec rm -rf {} +
	rm -rf build dist *.egg-info .pytest_cache .ruff_cache
