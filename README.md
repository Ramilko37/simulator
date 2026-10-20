# DroneTrainer — исходный проект Unreal Engine

[Скачать папку проекта ZIP](https://github.com/Ramilko37/simulator/archive/refs/heads/main.zip)

В корне находятся `DroneTrainer.uproject`, `Config/`, `Content/`, `Source/` и `Tests/`. Это исходный UE-проект с C++-кодом, картой и ассетами. Проектных плагинов `Plugins/` нет. Движок, Binaries, Intermediate, Saved и DerivedDataCache в репозиторий не включены.

Использовался Unreal Engine 5.8.3. Откройте `DroneTrainer.uproject` в Unreal Editor. Поскольку проект содержит C++, для компиляции на Windows нужны инструменты C++ Visual Studio. Сборка на Windows не выполнялась.

Только в передаваемой копии отключены необязательные редакторские плагины `ModelContextProtocol` и `AllToolsets`: они служат для связи с ИИ и установлены вне проекта на исходном Mac. Локальный токен Android File Server удалён. Игровой код и бинарные ассеты сохранены без изменений.

Ранее опубликованная Mac-сборка остаётся в [Releases](https://github.com/Ramilko37/simulator/releases).
