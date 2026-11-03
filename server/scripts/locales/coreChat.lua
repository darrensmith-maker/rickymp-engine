local dictionary = {
    EN = {
        color_menu_title = color.White .. "=== Choose nickname color ===\n\n" .. color.Gray .. "Select a color from the list",
        color_selected = "Selected color: [{index}] {name}",
        color_name_01 = "Sky blue", color_name_02 = "Dark sky blue",
        color_name_03 = "Medium sea green", color_name_04 = "Dark sea green",
        color_name_05 = "Pink", color_name_06 = "Dark pink",
        color_name_07 = "Khaki", color_name_08 = "Dark khaki",
        color_name_09 = "Salmon", color_name_10 = "Dark salmon",
        color_name_11 = "Turquoise", color_name_12 = "Dark turquoise",
        color_name_13 = "Goldenrod", color_name_14 = "Dark goldenrod",
        color_name_15 = "Rosy brown", color_name_16 = "Dark rosy brown",
        color_name_17 = "Thistle", color_name_18 = "Dark thistle",
        color_name_19 = "Mint", color_name_20 = "Dark mint",
        color_name_21 = "Honeydew", color_name_22 = "Dark honeydew",
        color_name_23 = "Light steel blue", color_name_24 = "Dark light steel blue",
        color_name_25 = "Plum", color_name_26 = "Dark plum",
        color_name_27 = "Peach puff", color_name_28 = "Dark peach puff",
        color_name_29 = "Wheat", color_name_30 = "Dark wheat",
        color_name_31 = "Burly wood", color_name_32 = "Dark burly wood",
        color_name_33 = "Pale goldenrod", color_name_34 = "Dark pale goldenrod",
        color_name_35 = "Antique white", color_name_36 = "Dark antique white",
        color_name_37 = "Moccasin", color_name_38 = "Dark moccasin",
        color_name_39 = "Light sky blue", color_name_40 = "Dark light sky blue",
        channel_whisper = "Whisper mode enabled.",
        channel_speak = "Normal speech mode enabled.",
        channel_shout = "Shout mode enabled.",
        channel_global = "Global OOC chat enabled.",
        channel_local_ooc = "Local OOC chat enabled.",
        whisper_tag = " [quietly]",
        shout_tag = " [loudly]",
        rp_enabled = "RP mode: ON\nSystem chat is hidden. Only RP players are shown in chat; non-RP player messages arrive as popups.",
        rp_disabled = "RP mode: OFF\nNormal chat and system messages are visible again.",
        rp_required = "You must enable RP mode to use global RP messages.",
        ooc_blocked_in_rp = "OOC chat is unavailable while RP mode is enabled.",
        rp_global_from = "Global RP message from {name}:\n{message}",
        popup_enabled = "Popup mode enabled",
        popup_disabled = "Popup mode disabled",
        cross_rp_footer = "\n\n---\nRP message preview is limited to {limit} characters. Use /rp to switch RP mode.",
        cross_ooc_footer = "\n\n---\nNon-RP message preview is limited to {limit} characters. Use /rp to switch RP mode.",
        admin_only = "This channel is available to administrators only.",
        admin_popup_usage = "Usage: //// <message>",
        try_0 = "(Very bad)", try_1 = "(Bad)", try_2 = "(Good)", try_3 = "(Very good)",
        help_title = color.Gold .. "=== CHAT HELP ===\n\n",
        help_text = color.Orange .. "SPEECH\n" ..
            color.White .. "/w [text]" .. color.Gray .. " - whisper\n" ..
            color.White .. "/s [text]" .. color.Gray .. " - normal speech\n" ..
            color.White .. "/sh [text]" .. color.Gray .. " - shout\n" ..
            color.White .. "// [text]" .. color.Gray .. " - local OOC; without text switches channel\n" ..
            color.White .. "/// [text]" .. color.Gray .. " - global OOC; without text switches channel\n" ..
            color.White .. "/color" .. color.Gray .. " - nickname color\n" ..
            color.White .. "/popup" .. color.Gray .. " - toggle popup display\n\n" ..
            color.Orange .. "ROLEPLAY\n" ..
            color.White .. "/rp" .. color.Gray .. " - toggle RP mode; hides system chat and isolates RP chat\n" ..
            color.White .. "/rp <text>" .. color.Gray .. " - global RP message\n" ..
            color.White .. "/me <action>" .. color.Gray .. " - character action\n" ..
            color.White .. "/do <description>" .. color.Gray .. " - scene/environment\n" ..
            color.White .. "/try <action>" .. color.Gray .. " - action with random result",
        players_online = "Players online: {count}",
        list_location = "Location", list_region = "Region", list_hidden = "Hidden",
        list_ping = "Ping", list_on_server = "On server", list_online = "Online",
        list_level = "lvl", list_less_minute = "< 1 min", list_min = "min", list_hour = "h",
        ghost_enabled = "Your location is hidden in /list.",
        ghost_disabled = "Your location is visible in /list.",
        ghost_marker = "[Ghost Mode]",
        day_one = "day", day_many = "days",
        unknown_race = "Unknown race"
    },
    RU = {
        color_menu_title = color.White .. "=== Выберите цвет никнейма ===\n\n" .. color.Gray .. "Выберите цвет из списка",
        color_selected = "Выбран цвет: [{index}] {name}",
        color_name_01 = "Небесно-голубой", color_name_02 = "Тёмный небесно-голубой",
        color_name_03 = "Средний морской зелёный", color_name_04 = "Тёмный морской зелёный",
        color_name_05 = "Розовый", color_name_06 = "Тёмно-розовый",
        color_name_07 = "Хаки", color_name_08 = "Тёмный хаки",
        color_name_09 = "Лососевый", color_name_10 = "Тёмный лососевый",
        color_name_11 = "Бирюзовый", color_name_12 = "Тёмно-бирюзовый",
        color_name_13 = "Золотарник", color_name_14 = "Тёмный золотарник",
        color_name_15 = "Розовато-коричневый", color_name_16 = "Тёмный розовато-коричневый",
        color_name_17 = "Чертополох", color_name_18 = "Тёмный чертополох",
        color_name_19 = "Мятный", color_name_20 = "Тёмно-мятный",
        color_name_21 = "Медовая роса", color_name_22 = "Тёмная медовая роса",
        color_name_23 = "Светло-стальной синий", color_name_24 = "Тёмный светло-стальной синий",
        color_name_25 = "Сливовый", color_name_26 = "Тёмно-сливовый",
        color_name_27 = "Персиковый", color_name_28 = "Тёмно-персиковый",
        color_name_29 = "Пшеничный", color_name_30 = "Тёмно-пшеничный",
        color_name_31 = "Древесный", color_name_32 = "Тёмно-древесный",
        color_name_33 = "Бледно-золотой", color_name_34 = "Тёмный бледно-золотой",
        color_name_35 = "Античный белый", color_name_36 = "Тёмный античный белый",
        color_name_37 = "Мокасиновый", color_name_38 = "Тёмный мокасиновый",
        color_name_39 = "Светло-небесный", color_name_40 = "Тёмный светло-небесный",
        channel_whisper = "Вы перешли в режим шёпота.",
        channel_speak = "Вы перешли в режим обычной речи.",
        channel_shout = "Вы перешли в режим крика.",
        channel_global = "Включён глобальный OOC-чат.",
        channel_local_ooc = "Включён локальный OOC-чат.",
        whisper_tag = " [тихо]",
        shout_tag = " [громко]",
        rp_enabled = "RP-режим: ВКЛ\nСистемный чат скрыт. В чате видны только RP-игроки; сообщения non-RP игроков приходят в MessageBox.",
        rp_disabled = "RP-режим: ВЫКЛ\nОбычный чат и системные сообщения снова отображаются.",
        rp_required = "Чтобы отправлять глобальные RP-сообщения, включите RP-режим.",
        ooc_blocked_in_rp = "OOC-чат недоступен, пока включён RP-режим.",
        rp_global_from = "Глобальное RP-сообщение от {name}:\n{message}",
        popup_enabled = "Режим popup включён",
        popup_disabled = "Режим popup выключен",
        cross_rp_footer = "\n\n---\nПредпросмотр RP-сообщения ограничен {limit} символами. Для смены режима используйте /rp.",
        cross_ooc_footer = "\n\n---\nПредпросмотр non-RP сообщения ограничен {limit} символами. Для смены режима используйте /rp.",
        admin_only = "Этот канал доступен только администраторам.",
        admin_popup_usage = "Использование: //// <сообщение>",
        try_0 = "(Очень плохо)", try_1 = "(Плохо)", try_2 = "(Хорошо)", try_3 = "(Очень хорошо)",
        help_title = color.Gold .. "=== СПРАВКА ПО ЧАТУ ===\n\n",
        help_text = color.Orange .. "РЕЧЬ\n" ..
            color.White .. "/w [текст]" .. color.Gray .. " - шёпот\n" ..
            color.White .. "/s [текст]" .. color.Gray .. " - обычная речь\n" ..
            color.White .. "/sh [текст]" .. color.Gray .. " - крик\n" ..
            color.White .. "// [текст]" .. color.Gray .. " - локальный OOC; без текста переключает канал\n" ..
            color.White .. "/// [текст]" .. color.Gray .. " - глобальный OOC; без текста переключает канал\n" ..
            color.White .. "/color" .. color.Gray .. " - цвет имени\n" ..
            color.White .. "/popup" .. color.Gray .. " - режим всплывающих сообщений\n\n" ..
            color.Orange .. "РОЛЕВАЯ ИГРА\n" ..
            color.White .. "/rp" .. color.Gray .. " - RP-режим: скрывает системный чат и изолирует RP-сообщения\n" ..
            color.White .. "/rp <текст>" .. color.Gray .. " - глобальное RP-сообщение\n" ..
            color.White .. "/me <действие>" .. color.Gray .. " - действие персонажа\n" ..
            color.White .. "/do <описание>" .. color.Gray .. " - описание обстановки\n" ..
            color.White .. "/try <действие>" .. color.Gray .. " - попытка со случайным результатом",
        players_online = "Игроков на сервере: {count}",
        list_location = "Локация", list_region = "Регион", list_hidden = "Скрыта",
        list_ping = "Пинг", list_on_server = "На сервере", list_online = "Онлайн",
        list_level = "ур.", list_less_minute = "< 1 мин", list_min = "мин", list_hour = "ч",
        ghost_enabled = "Вы скрыли своё местоположение в /list.",
        ghost_disabled = "Вы отображаете своё местоположение в /list.",
        ghost_marker = "[Режим Ghost]",
        day_one = "день", day_few = "дня", day_many = "дней",
        unknown_race = "Неизвестная раса"
    }
}

return dictionary
