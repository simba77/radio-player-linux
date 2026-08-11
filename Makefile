PYTHON ?= python3
# Инструменты разработки живут в .venv (создан с --system-site-packages),
# потому что дистрибутивный Python запрещает ставить пакеты глобально (PEP 668).
VENV := .venv
VENV_PY := $(VENV)/bin/python

PREFIX ?= $(HOME)/.local

.PHONY: help run probe deps devenv lint test install uninstall deb clean

help:
	@echo "make deps      — установить системные зависимости (нужен sudo)"
	@echo "make devenv    — создать .venv с pytest и ruff"
	@echo "make run       — запустить приложение"
	@echo "make probe     — диагностика метаданных потока (PLAN.md §2.1)"
	@echo "make lint      — ruff"
	@echo "make test      — pytest"
	@echo "make install   — установить в $(PREFIX)"
	@echo "make uninstall — удалить из $(PREFIX)"
	@echo "make deb       — собрать .deb в dist/"

devenv:
	$(PYTHON) -m venv --system-site-packages $(VENV)
	$(VENV)/bin/pip install --quiet --upgrade pytest ruff

deps:
	sudo apt install -y libmpv2 python3-mpv \
		python3-pyside6.qtcore python3-pyside6.qtgui python3-pyside6.qtwidgets \
		python3-pyside6.qtnetwork python3-pyside6.qtdbus

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

install:
	PREFIX=$(PREFIX) sh scripts/install.sh

uninstall:
	rm -rf $(PREFIX)/lib/radio-player
	rm -f $(PREFIX)/bin/radio-player
	rm -f $(PREFIX)/share/applications/radio-player.desktop
	@echo "Удалено из $(PREFIX). Настройки в ~/.config/radio-player остались."

deb:
	sh scripts/build_deb.sh

clean:
	find . -name __pycache__ -type d -prune -exec rm -rf {} +
	rm -rf build dist *.egg-info .pytest_cache .ruff_cache
