PYTHON ?= python3
# Инструменты разработки (clang-format, gcovr, shellcheck) живут в .venv,
# потому что дистрибутивный Python запрещает ставить пакеты глобально (PEP 668).
VENV := .venv
VENV_PY := $(VENV)/bin/python

PREFIX ?= $(HOME)/.local

# Отладочная сборка для работы, выпускная — для установки и пакетов, и отдельная
# с санитайзерами и покрытием для `make check`.
BUILD_DIR := build/debug
RELEASE_DIR := build/release
CHECKS_DIR := build/checks

CLANG_FORMAT := $(VENV)/bin/clang-format
CXX_SOURCES = $(shell find src tests -name '*.cpp' -o -name '*.hpp')

.PHONY: help deps devenv build run test check lint shellcheck sanitize format i18n-update icons release install uninstall deb clean

help:
	@echo "make deps        — установить зависимости сборки (нужен sudo)"
	@echo "make devenv      — создать .venv с инструментами разработки"
	@echo "make build       — собрать отладочную версию в $(BUILD_DIR)"
	@echo "make run         — собрать и запустить"
	@echo "make test        — собрать и прогнать тесты"
	@echo "make check       — форматирование, скрипты, тесты под ASan/UBSan и покрытие — то, что гоняет CI"
	@echo "make lint        — проверить форматирование (clang-format)"
	@echo "make shellcheck  — shellcheck для скриптов"
	@echo "make format      — отформатировать код"
	@echo "make i18n-update — обновить translations/*.ts строками из кода"
	@echo "make icons       — перерисовать PNG из SVG-иконки"
	@echo "make install     — установить в $(PREFIX)"
	@echo "make uninstall   — удалить из $(PREFIX)"
	@echo "make deb         — собрать .deb и .tar.gz в dist/"

deps:
	sudo apt install -y cmake ninja-build g++ pkg-config dpkg-dev file \
		qt6-base-dev qt6-tools-dev qt6-l10n-tools libqt6svg6 qt6-translations-l10n \
		libmpv-dev catch2 dbus

devenv:
	$(PYTHON) -m venv $(VENV)
	$(VENV)/bin/pip install --quiet --requirement requirements-dev.txt

$(VENV_PY):
	$(MAKE) devenv

build:
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR)

run: build
	$(BUILD_DIR)/radio-player --log-level debug

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure -j $(shell nproc)

check: lint shellcheck sanitize

lint: $(VENV_PY)
	$(CLANG_FORMAT) --dry-run --Werror $(CXX_SOURCES)

shellcheck: $(VENV_PY)
	$(VENV)/bin/shellcheck scripts/*.sh

# Тесты под AddressSanitizer и UndefinedBehaviorSanitizer с измерением покрытия:
# санитайзеры ловят ошибки памяти, которых не видно в обычном прогоне, а gcovr
# не даёт покрытию просесть (порог — в gcovr.cfg). Подавления утечек — только
# для чужих библиотек.
sanitize: $(VENV_PY)
	cmake -S . -B $(CHECKS_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Debug -DRADIO_SANITIZE=ON -DRADIO_COVERAGE=ON
	@# Счётчики прошлого прогона: после пересборки они не сходятся с кодом.
	find $(CHECKS_DIR) -name '*.gcda' -delete
	cmake --build $(CHECKS_DIR)
	LSAN_OPTIONS=suppressions=$(CURDIR)/tests/lsan.supp ctest --test-dir $(CHECKS_DIR) --output-on-failure -j $(shell nproc)
	$(VENV)/bin/gcovr $(CHECKS_DIR)

format: $(VENV_PY)
	$(CLANG_FORMAT) -i $(CXX_SOURCES)

# Переводы правятся в Qt Linguist; .qm собираются при сборке и встраиваются
# в программу, поэтому в репозиторий идут только .ts.
i18n-update: build
	cmake --build $(BUILD_DIR) --target i18n_update

icons:
	sh scripts/render_icons.sh

release:
	cmake -S . -B $(RELEASE_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release -DRADIO_BUILD_TESTS=OFF
	cmake --build $(RELEASE_DIR)

install: release
	cmake --install $(RELEASE_DIR) --prefix $(PREFIX)
	@# Кэши оболочки: без обновления ярлык и иконка появятся только после перелогина.
	@# Команд может не быть — установка от этого не должна падать.
	-@command -v update-desktop-database >/dev/null && update-desktop-database "$(PREFIX)/share/applications" 2>/dev/null
	-@command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t -f "$(PREFIX)/share/icons/hicolor" 2>/dev/null
	@case ":$$PATH:" in *":$(PREFIX)/bin:"*) ;; *) echo "Внимание: $(PREFIX)/bin отсутствует в PATH." ;; esac

uninstall:
	PREFIX=$(PREFIX) sh scripts/uninstall.sh

deb: release
	cpack --config $(RELEASE_DIR)/CPackConfig.cmake -B dist

clean:
	rm -rf build dist
