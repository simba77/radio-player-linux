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

.PHONY: help run probe deps deps-i18n devenv lint test i18n-update i18n-release install uninstall deb clean

help:
	@echo "make deps      — установить системные зависимости (нужен sudo)"
	@echo "make deps-i18n — инструменты для работы над переводами (нужен sudo)"
	@echo "make devenv    — создать .venv с pytest и ruff"
	@echo "make run       — запустить приложение"
	@echo "make probe     — диагностика метаданных живого потока"
	@echo "make lint      — ruff"
	@echo "make test      — pytest"
	@echo "make i18n-update  — обновить .ts новыми строками из кода"
	@echo "make i18n-release — собрать .qm из .ts"
	@echo "make install   — установить в $(PREFIX)"
	@echo "make uninstall — удалить из $(PREFIX)"
	@echo "make deb       — собрать .deb в dist/"

devenv:
	$(PYTHON) -m venv --system-site-packages $(VENV)
	$(VENV)/bin/pip install --quiet --upgrade pytest ruff

deps:
	sudo apt install -y libmpv2 python3-mpv \
		python3-pyside6.qtcore python3-pyside6.qtgui python3-pyside6.qtwidgets \
		python3-pyside6.qtnetwork python3-pyside6.qtdbus \
		qt6-translations-l10n

# Только для работы над переводами: lupdate с поддержкой Python.
deps-i18n:
	sudo apt install -y pyside6-tools qt6-l10n-tools qt6-tools

run:
	$(PYTHON) -m radio_player

probe:
	$(PYTHON) scripts/probe_metadata.py $(URL)

lint: $(VENV_PY)
	$(VENV)/bin/ruff check .

test: $(VENV_PY)
	$(VENV_PY) -m pytest

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

deb:
	sh scripts/build_deb.sh

clean:
	find . -name __pycache__ -type d -prune -exec rm -rf {} +
	rm -rf build dist *.egg-info .pytest_cache .ruff_cache
