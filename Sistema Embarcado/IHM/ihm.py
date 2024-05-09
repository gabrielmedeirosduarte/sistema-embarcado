# Autores: Gabriel M. Duarte e Kléber R. S. Júnior
# Data: 08 de maio de 2024

# Bibliotecas:
import serial 
from time import sleep
import tkinter as tk
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure
from threading import Thread
from collections import deque
import numpy as np

# Função para abrir a porta serial:
def open_serial_port(port, baud_rate):
    while True:
        try:
            ser = serial.Serial(port, baud_rate)
            sleep(1)
            ser.flushInput()
            return ser
        except serial.SerialException as e:
            print(f"Aguardando conexão com a porta serial, Erro:{e}")
            sleep(1)

# Configurações iniciais:
port = 'COM3'
baud_rate = 921600
ser = open_serial_port(port, baud_rate)

# Inicialização de variáveis: 
angles = []
duties = []
timers = []
data = []
file_data = []

angle_float = 0.0
duty_float = 0.0
timer_int = 0
stop_thread = False

# Tempo máximo para visualização no gráfico dinâmico:
max_time = 10000 

# Taxa de aostragem do gráfico dinâmico:
plot_sample = 50 

# Filas para armazenamento dos dados a serem plotados:
plot_timer = deque(maxlen=int(max_time/plot_sample))
plot_angles = deque(maxlen=int(max_time/plot_sample))
plot_duties = deque(maxlen=int(max_time/plot_sample))

# Função para ler os dados da porta serial:
def read_data():
    global stop_thread
    if not stop_thread:
        global angles
        global duties
        global timers
        global ser
        global data
        global file_data
        global angle_float
        global duty_float
        global timer_int

        try:
            while ser.is_open:
                try:
                    data = str(ser.readline().decode('utf8')).rstrip("\n")
                    if "," in data:
                        parts = data.split(',')
                        try:
                            angle_float = float(parts[0])
                            duty_float = float(parts[1])
                            timer_int = int(parts[2])
                        
                            angles.append(angle_float)
                            duties.append(duty_float)
                            timers.append(timer_int)

                            if timer_int % plot_sample == 0:
                                plot_timer.append(timer_int)
                                plot_duties.append(duty_float)
                                plot_angles.append(angle_float)

                            file_data.append(str(f"Angle: {angle_float:.4f}, Duty_cycle: {duty_float:.4f}%, Timer: {timer_int}ms\n"))

                        except Exception as error:
                            print(f"Erro: {error}") 
                    elif 'Fim' in data:
                        print("Encerrou")
                        exit(1)
                except serial.SerialException:
                    print("Erro de leitura ...")
                    break
            if not ser.is_open:
                ser.close()
                print("Close ...")
        except serial.SerialException:
            print("Erro de conexão...")
    else:
        exit(1)

# Função para iniciar a comunicação com o dispositivo:
def start():
    ser.write(b's')
    print("Dados enviados!")

# Função para fechar a janela e encerrar a execução:
def close_window():
    global stop_thread
    ser.write(b'e')
    process_and_plot()
    if file_data:
        save_data()
    window.destroy()
    stop_thread = True
    exit(1)

# Função para salvar os dados em arquivo txt:
def save_data(file_path='dados_recebidos.txt'):
    with open(file_path, 'w') as file:
        for dado in file_data:
            file.write(data)

# Função para salvar o gráfico completo como imagem:
def save_graph(fig, file_name='grafico.png'):
    fig.savefig(file_name)
    print(f'Gráfico salvo como "{file_name}".')

# Função para atualizar a janela com os dados:
def window_data():
    if not stop_thread:
        global max_time
        step = 20000

        if max_time < timer_int:
            ax2.set_xlim(max_time, max_time + step)
            ax1.set_xlim(max_time, max_time + step)
            max_time = max_time + step
            
        ax1.plot(plot_timer, plot_angles, color='blue')
        ax2.plot(plot_timer, plot_duties, color='black')
        fig.canvas.draw()
        
        window.after(200, window_data)
    else:
        exit(1)

# Função para processar os dados e plotar o gráfico completo:
def process_and_plot():
    if timers:
        figur = plt.figure()
        plt.subplot(2, 1, 1)
        plt.plot(timers, angles, color='blue', label='Ângulo')
        plt.ylabel('angle [°]')
        plt.legend()
        plt.grid()
        plt.subplot(2, 1, 2)
        plt.plot(timers, duties, color='black')
        plt.xlabel('time [ms]')
        plt.ylabel('duty cycle [%]')
        plt.grid()
        plt.show()
        figur.savefig('Grafico_completo.png')
    else:
        print("Vetor vazio")

# Configurações da janela Tkinter:
font = ('Times News Roman', 10, 'bold')
window = tk.Tk()
window.geometry('1360x768')
window.title("Janela de dados")

# Criação dos botões na janela Tkinter:
frame_buttons = tk.Frame(window)
frame_buttons.pack(side=tk.LEFT,fill='x')
save_button = tk.Button(frame_buttons, text="Salvar Gráfico", command=lambda: save_graph(fig), width=20, height=2, font= font, relief=tk.RAISED, borderwidth=5)
close_button = tk.Button(frame_buttons, text="Fechar Janela", command=close_window, width=20, height=2, font= font, relief=tk.RAISED, borderwidth=5)
start_button = tk.Button(frame_buttons, text="Iniciar", command=start, width=20, height=2, font= font, relief=tk.RAISED, borderwidth=5)
save_button.grid(row = 0, column=0, pady=10, )
close_button.grid(row = 1, column=0, pady=10)
start_button.grid(row =2, column=0, pady=10)

# Configurações do plot:
fig = Figure(figsize=(5, 4), dpi=100)
canvas = FigureCanvasTkAgg(fig, master=window)
canvas.get_tk_widget().pack(side=tk.TOP, fill=tk.BOTH, expand=1)

ax1 = fig.add_subplot(2, 1, 1)
ax1.grid()
ax1.set_ylim(0, 90)
ax1.set_ylabel('Sensor [V]')
ax1.set_xlim(0, max_time)

ax2 = fig.add_subplot(2, 1, 2)
ax2.grid()
ax2.set_ylim(0, 100)
ax2.set_ylabel('PWM [V]')
ax2.set_xlim(0, max_time)
ax2.set_xlabel('Tempo [ms]')

# Inicialização das threads para ler os dados da porta serial e atualizar a janela Tkinter:
new_thread_ler_dados = Thread(target=read_data)
new_thread_ler_dados.daemon = True
new_thread_ler_dados.start()

new_thread_tkinter = Thread(target=window_data)
new_thread_tkinter.daemon = True
new_thread_tkinter.start()

window.mainloop() 
ser.close()
exit(1)