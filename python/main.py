import sys
import glob
import serial
import pyautogui
import tkinter as tk
from tkinter import ttk
from tkinter import messagebox
from time import sleep

TOLERANCIA = 10   # Tolerância mínima para o volante

MAX_KEY_PRESS = 10  # Máximo de vezes que uma tecla pode ser pressionada sem resetar

def controle(ser):
    """Lê pacotes UART e simula teclas."""

    MAX_KEY_PRESSES = 7
    MAX_KEY_PRESS = 10

    key_states = {
        'w': 0,
        's': 0,
        'a': 0,
        'd': 0,
        '1': 0,
        '2': 0,
        '3': 0,
        '4': 0
    }

    key_counters = {
        'a': 0,
        'd': 0,
        'w': 0,
        's': 0
    }

    last_button_states = {
        '1': 0,
        '2': 0,
        '3': 0,
        '4': 0
    }

    pyautogui.PAUSE = 0

    while True:
        try:
            pacote = ser.read(10)

            if len(pacote) != 10:
                sleep(0.01)
                continue

            if pacote[9] != 0xFF:
                continue

            direcao = int.from_bytes(pacote[0:1], byteorder='little', signed=True)
            freio = int.from_bytes(pacote[1:3], byteorder='big', signed=False)
            acelerador = int.from_bytes(pacote[3:5], byteorder='big', signed=False)
            btn_x = pacote[5]
            btn_triangle = pacote[6]
            btn_circle = pacote[7]
            btn_square = pacote[8]

            print(f"Volante: {direcao} | Acelerador: {acelerador} | Freio: {freio} | X: {btn_x} | Triangle: {btn_triangle} | Circle: {btn_circle} | Square: {btn_square}")

            if direcao > TOLERANCIA:
                if key_counters['a'] < MAX_KEY_PRESSES:
                    key_states["d"] = 0
                    key_states["a"] = 1
                    key_counters['a'] += 1
                else:
                    key_states["a"] = 0
            elif direcao < -TOLERANCIA:
                if key_counters['d'] < MAX_KEY_PRESSES:
                    key_states["a"] = 0
                    key_states["d"] = 1
                    key_counters['d'] += 1
                else:
                    key_states["d"] = 0
            else:
                key_states["a"] = 0
                key_states["d"] = 0
                key_counters['a'] = 0
                key_counters['d'] = 0

            if acelerador > 600:
                if key_counters['w'] < MAX_KEY_PRESSES:
                    key_states["w"] = 1
                    key_states["s"] = 0
                    key_counters['w'] += 1
                else:
                    key_states["w"] = 0
            else:
                key_states["w"] = 0
                key_counters['w'] = 0

            if freio > 600:
                if key_counters['s'] < MAX_KEY_PRESSES:
                    key_states["s"] = 1
                    key_states["w"] = 0
                    key_counters['s'] += 1
                else:
                    key_states["s"] = 0
            else:
                key_states["s"] = 0
                key_counters['s'] = 0

            # Atualizar estados dos botões
            key_states["1"] = btn_x
            key_states["2"] = btn_square
            key_states["3"] = btn_triangle
            key_states["4"] = btn_circle

            if key_states["a"] == 1:
            # Direção
                if key_counters["a"] < MAX_KEY_PRESS:
                    pyautogui.keyDown("a")
                    pyautogui.keyUp("d")
                    key_counters["a"] += 1
            else:
                pyautogui.keyUp("a")
                key_counters["a"] = 0 

            if key_states["d"] == 1:
                if key_counters["d"] < MAX_KEY_PRESS:
                    pyautogui.keyDown("d")
                    pyautogui.keyUp("a")
                    key_counters["d"] += 1
            else:
                pyautogui.keyUp("d")
                key_counters["d"] = 0  

            # Acelerador e freio: 'w' e 's'
            if key_states["w"] == 1:
                if key_counters["w"] < MAX_KEY_PRESS:
                    pyautogui.keyDown("w")
                    pyautogui.keyUp("s")
                    key_counters["w"] += 1
            else:
                pyautogui.keyUp("w")
                key_counters["w"] = 0

            if key_states["s"] == 1:
                if key_counters["s"] < MAX_KEY_PRESS:
                    pyautogui.keyDown("s")
                    pyautogui.keyUp("w")
                    key_counters["s"] += 1
            else:
                pyautogui.keyUp("s")
                key_counters["s"] = 0

            if key_states["1"] and not last_button_states["1"]:
                pyautogui.keyDown("space")
            elif not key_states["1"] and last_button_states["1"]:
                pyautogui.keyUp("space")

            if key_states["2"] and not last_button_states["2"]:
                pyautogui.keyDown("c")
            elif not key_states["2"] and last_button_states["2"]:
                pyautogui.keyUp("c")

            if key_states["3"] and not last_button_states["3"]:
                pyautogui.keyDown("r")
            elif not key_states["3"] and last_button_states["3"]:
                pyautogui.keyUp("r")

            if key_states["4"] and not last_button_states["4"]:
                pyautogui.keyDown("shiftleft")
            elif not key_states["4"] and last_button_states["4"]:
                pyautogui.keyUp("shiftleft")


            last_button_states["1"] = key_states["1"]
            last_button_states["2"] = key_states["2"]
            last_button_states["3"] = key_states["3"]
            last_button_states["4"] = key_states["4"]

            sleep(0.01)

            # ser.reset_input_buffer()
        except Exception as e:
            print(f"Erro: {e}")
            sleep(0.1)


def serial_ports():
    """Retorna uma lista das portas seriais disponíveis na máquina."""
    ports = []
    if sys.platform.startswith('win'):
        # Windows
        for i in range(1, 256):
            port = f'COM{i}'
            try:
                s = serial.Serial(port)
                s.close()
                ports.append(port)
            except (OSError, serial.SerialException):
                pass
    elif sys.platform.startswith('linux') or sys.platform.startswith('cygwin'):
        # Linux/Cygwin
        ports = glob.glob('/dev/tty[A-Za-z]*') + glob.glob('/dev/rfcomm*')

    elif sys.platform.startswith('darwin'):
        # macOS
        ports = glob.glob('/dev/tty.*')
    else:
        raise EnvironmentError(
            'Plataforma não suportada para detecção de portas seriais.')

    result = []
    for port in ports:
        try:
            s = serial.Serial(port)
            s.close()
            result.append(port)
        except (OSError, serial.SerialException):
            pass

    return result

def conectar_porta(port_name, root, botao_conectar, status_label, mudar_cor_circulo):
    """Abre a conexão com a porta selecionada e inicia o loop de leitura."""
    
    # ser_reserva = serial.Serial('/dev/ttyACM0', 115200) #teste
    if not port_name:
        messagebox.showwarning(
            "Aviso", "Selecione uma porta serial antes de conectar.")
        return

    try:

        ser = serial.Serial(port_name, 115200, timeout=1)
        status_label.config(
            text=f"Conectado em {port_name}", foreground="green")
        mudar_cor_circulo("green")
        # Update button text to indicate connection
        botao_conectar.config(text="Conectado")
        root.update()

        status_led = 1  # Envia 1 byte
        ser.write(bytes([status_led]))

        # Inicia o loop de leitura (bloqueante).
        controle(ser)

    except KeyboardInterrupt:
        print("Encerrando via KeyboardInterrupt.")
    except Exception as e:
        messagebox.showerror(
            "Erro de Conexão", f"Não foi possível conectar em {port_name}.\nErro: {e}")
        mudar_cor_circulo("red")
    finally:
        ser = serial.Serial(port_name, 115200, timeout=1)

        status_led = 0  # Envia 1 byte
        ser.write(bytes([status_led]))

        ser.close()
        status_label.config(text="Conexão encerrada.", foreground="red")
        mudar_cor_circulo("red")

def criar_janela():
    root = tk.Tk()
    root.title("Controle de Mouse")
    root.geometry("400x250")
    root.resizable(False, False)

    # Dark mode color settings
    dark_bg = "#2e2e2e"
    dark_fg = "#ffffff"
    accent_color = "#007acc"
    root.configure(bg=dark_bg)

    style = ttk.Style(root)
    style.theme_use("clam")
    style.configure("TFrame", background=dark_bg)
    style.configure("TLabel", background=dark_bg,
                    foreground=dark_fg, font=("Segoe UI", 11))
    style.configure("TButton", font=("Segoe UI", 10, "bold"),
                    foreground=dark_fg, background="#444444", borderwidth=0)
    style.map("TButton", background=[("active", "#555555")])
    style.configure("Accent.TButton", font=("Segoe UI", 12, "bold"),
                    foreground=dark_fg, background=accent_color, padding=6)
    style.map("Accent.TButton", background=[("active", "#005f9e")])

    # Updated combobox styling to match the dark GUI color
    style.configure("TCombobox",
                    fieldbackground=dark_bg,
                    background=dark_bg,
                    foreground=dark_fg,
                    padding=4)
    style.map("TCombobox", fieldbackground=[("readonly", dark_bg)])

    # Main content frame (upper portion)
    frame_principal = ttk.Frame(root, padding="20")
    frame_principal.pack(expand=True, fill="both")

    titulo_label = ttk.Label(
        frame_principal, text="Controle de Mouse", font=("Segoe UI", 14, "bold"))
    titulo_label.pack(pady=(0, 10))

    porta_var = tk.StringVar(value="")

    botao_conectar = ttk.Button(
        frame_principal,
        text="Conectar e Iniciar Leitura",
        style="Accent.TButton",
        command=lambda: conectar_porta(
            porta_var.get(), root, botao_conectar, status_label, mudar_cor_circulo)
    )
    botao_conectar.pack(pady=10)

    # Create footer frame with grid layout to host status label, port dropdown, and status circle
    footer_frame = tk.Frame(root, bg=dark_bg)
    footer_frame.pack(side="bottom", fill="x", padx=10, pady=(10, 0))

    # Left: Status label
    status_label = tk.Label(footer_frame, text="Aguardando seleção de porta...", font=("Segoe UI", 11),
                            bg=dark_bg, fg=dark_fg)
    status_label.grid(row=0, column=0, sticky="w")

    # Center: Port selection dropdown
    portas_disponiveis = serial_ports()
    if portas_disponiveis:
        porta_var.set(portas_disponiveis[0])
    port_dropdown = ttk.Combobox(footer_frame, textvariable=porta_var,
                                 values=portas_disponiveis, state="readonly", width=10)
    port_dropdown.grid(row=0, column=1, padx=10)

    # Right: Status circle (canvas)
    circle_canvas = tk.Canvas(footer_frame, width=20,
                              height=20, highlightthickness=0, bg=dark_bg)
    circle_item = circle_canvas.create_oval(
        2, 2, 18, 18, fill="red", outline="")
    circle_canvas.grid(row=0, column=2, sticky="e")

    footer_frame.columnconfigure(1, weight=1)

    def mudar_cor_circulo(cor):
        circle_canvas.itemconfig(circle_item, fill=cor)

    root.mainloop()

if __name__ == "__main__":
    criar_janela()
