from pathlib import Path
from reportlab.pdfgen import canvas
from reportlab.lib.pagesizes import A3, landscape
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'output/pdf/bic-call-structure.pdf'
OUT.parent.mkdir(parents=True, exist_ok=True)
pdfmetrics.registerFont(TTFont('Arial', 'C:/Windows/Fonts/arial.ttf'))
pdfmetrics.registerFont(TTFont('ArialBold', 'C:/Windows/Fonts/arialbd.ttf'))
W, H = landscape(A3)
c = canvas.Canvas(str(OUT), pagesize=(W, H))
c.setTitle('BIC - структура вызовов: было и стало')
c.setAuthor('BIC')
BLUE, RED, GRAY = '#2563EB', '#DC2626', '#64748B'
INK = '#172033'

def text(x, y, s, size=12, color=INK, bold=False):
    c.setFillColor(color)
    c.setFont('ArialBold' if bold else 'Arial', size)
    c.drawString(x, H-y, s)

def box(x, y, w, h, lines, color=GRAY, fill='#F1F5F9', size=11):
    c.setStrokeColor(color)
    c.setFillColor(fill)
    c.setLineWidth(1.2)
    c.roundRect(x, H-y-h, w, h, 7, fill=1, stroke=1)
    step = size + 4
    first = y + h/2 - (len(lines)-1)*step/2 + size*.35
    for i, line in enumerate(lines):
        assert pdfmetrics.stringWidth(line, 'ArialBold' if i == 0 else 'Arial', size) < w-18, line
        c.setFont('ArialBold' if i == 0 else 'Arial', size)
        c.setFillColor(INK)
        c.drawCentredString(x+w/2, H-first-i*step, line)

def arrow(points, color=GRAY, dashed=False):
    c.setStrokeColor(color)
    c.setFillColor(color)
    c.setLineWidth(1.25)
    c.setDash(4, 3) if dashed else c.setDash()
    p = c.beginPath()
    p.moveTo(points[0][0], H-points[0][1])
    for x, y in points[1:]: p.lineTo(x, H-y)
    c.drawPath(p)
    c.setDash()
    x, y = points[-1]
    px, py = points[-2]
    dx, dy = x-px, y-py
    length = (dx*dx+dy*dy)**.5
    dx, dy = dx/length, dy/length
    p = c.beginPath()
    p.moveTo(x, H-y)
    p.lineTo(x-6*dx+3*dy, H-(y-6*dy-3*dx))
    p.lineTo(x-6*dx-3*dy, H-(y-6*dy+3*dx))
    p.close()
    c.drawPath(p, fill=1, stroke=0)

text(36, 38, 'BIC / Структура вызовов', 23, bold=True)
text(36, 61, 'Открытие документов Office: до и после выделения общего модуля', 12)
for x, color, label in [(770, BLUE, 'Было'), (875, RED, 'Стало'), (980, GRAY, 'Общая часть')]:
    c.setFillColor(color); c.circle(x, H-36, 4, fill=1, stroke=0)
    text(x+12, 40, label, 11)

# Общая структура приложения. Подписанные ветви - маршруты сообщений Win32.
box(36, 84, 160, 44, ['wWinMain()'])
box(236, 84, 330, 44, ['SingleInstance', 'Повторный запуск: ActivateExistingWindow()'])
arrow([(196, 106), (236, 106)])
box(36, 153, 295, 49, ['Создание окна и цикл сообщений', 'CreateWindowExW() / DispatchMessageW()'])
arrow([(116, 128), (116, 153)])
box(372, 153, 380, 49, ['MainWindow::WindowProcedure()', 'Обработчик сообщений окна'])
arrow([(331, 177), (372, 177)])
box(794, 84, 360, 44, ['WM_CREATE → OnCreate()', 'Кнопки / TrayIcon::Add() / Hotkeys::Register()'])
box(794, 153, 360, 49, ['WM_HOTKEY / события трея', 'Handle() → Show() / Minimize()'])
box(794, 227, 360, 49, ['WM_DESTROY → OnDestroy()', 'Unregister() / Remove() / PostQuitMessage()'])
arrow([(752, 177), (773, 177), (773, 106), (794, 106)])
arrow([(752, 177), (794, 177)])
arrow([(752, 177), (773, 177), (773, 251), (794, 251)])
box(372, 227, 380, 49, ['WM_COMMAND → OnCommand()', 'Кнопка «Реестр договоров»'])
arrow([(562, 202), (562, 227)])

text(36, 313, 'БЫЛО / Открытие внутри команды реестра', 15, BLUE, True)
text(620, 313, 'СТАЛО / Параметры и общий модуль', 15, RED, True)
left, right, width = 36, 620, 534
old = [
 ['ContractRegistry::Open(owner)', 'Фиксированный путь к реестру XLSX; проверка файла'],
 ['OpenInExcel()', 'Без входных параметров'],
 ['CoInitializeEx() → GetActiveObject() / CoCreateInstance()', 'Подключение только к Excel'],
 ['Invoke(): AutomationSecurity / Visible / Workbooks.Open', 'Workbook.Activate()'],
 ['Excel.Application.Hwnd', 'ShowWindow() → SetWindowPos(): всегда левая половина'],
 ['Ручное освобождение COM-ресурсов', 'Возврат bool → общее сообщение об ошибке'],
]
new = [
 ['ContractRegistry::Open(owner) → OfficeDocument::Open(options)', 'OpenOptions: путь, приложение, окно, read_only, reuse_application'],
 ['Проверка параметров → ComScope', 'Инициализация COM; подключение к приложению'],
 ['SecurityScope::Enable() → Invoke(): Open → Restore()', 'Excel: Workbooks / Word: Documents / PowerPoint: Presentations'],
 ['PlaceWindow(document, options)', 'Windows.Item(1).Hwnd: окно конкретного документа'],
 ['ShowWindow() / SetWindowPos()', 'Keep / LeftHalf / RightHalf / Maximized / Rectangle'],
 ['OpenResult → DescribeError() → MessageBoxW() при ошибке', 'HRESULT, этап, признак открытия; автоматическая очистка COM'],
]
for x, rows, color, fill in [(left, old, BLUE, '#EFF6FF'), (right, new, RED, '#FFF1F2')]:
    arrow([(562, 276), (562, 290), (x+width-15, 290), (x+width-15, 330)], color)
    for i, lines in enumerate(rows):
        y = 330+i*65
        box(x, y, width, 48, lines, color, fill, 11)
        if i < len(rows)-1: arrow([(x+width/2, y+48), (x+width/2, y+65)], color)

box(620, 735, 534, 40, ['Будущие команды: другие таблицы, Word и PowerPoint'], RED, '#FFF1F2', 11)
arrow([(1154, 755), (1170, 755), (1170, 354), (1154, 354)], RED, True)
text(36, 749, 'Стрелки: вызовы и последовательность основных этапов.', 10, GRAY)
text(36, 768, 'Пунктир: подключение будущих команд к общему интерфейсу.', 10, GRAY)
text(36, 810, 'BIC • C++20 / Win32 / COM', 10, GRAY)
text(1070, 810, '1 / 1', 10, GRAY)
c.save()
print(OUT)

