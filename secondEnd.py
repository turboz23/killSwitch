import time
from importlib import import_module
from google import genai
import pyautogui
import serial
from serial.serialutil import SerialException
import os
from twilio.rest import Client

Client = import_module("twilio.rest").Client

# Initialize the Gemini client using the google-genai package
# PERSONAL DATA HIDDEN FOR PRIVACY REASONS
client = genai.Client(api_key="insert_api_key_here")

port = 'COM5'
baud = 9600
esp32 = None

# PERSONAL DATA HIDDEN FOR PRIVACY REASONS
TWILIO_ACCOUNT_SID = "sid"
TWILIO_AUTH_TOKEN = "token"

# Retry loop for initial COM port connection
print(f"Attempting to connect to {port}...")
while esp32 is None:
    try:
        esp32 = serial.Serial(port, baud, timeout=1)
        print(f"Successfully connected to {port}")
    except SerialException:
        print(f"Could not open port {port}. Retrying in 2 seconds...")
        time.sleep(2)

def analyze_screen():
    print("Capturing screen...")
    screenshot = pyautogui.screenshot()
    print("Screen captured successfully.")

    print("Sending to Gemini...")
    prompt = """
    You are a fraud detection expert protecting elderly users. 
    Analyze this screenshot of a marketplace or checkout screen. 
    Look for urgency tactics, fake UI elements, off-platform payment requests, or known scam patterns.
    Respond with ONLY a single number:
    100 = Completely safe/Normal
    101 = Suspicious, proceed with caution
    102 = High risk of fraud or scam
    """

    max_retries = 3
    success = False

    for attempt in range(max_retries):
        try:
            response = client.models.generate_content(
                model='gemini-3.8-flash',
                contents=[prompt, screenshot]
            )
            score = response.text.strip()
            print(f"Gemini says: {score}")

            # Send the exact severity code back to the ESP32
            esp32.write(f"{score}\n".encode())
            success = True
            break
        except Exception as e:
            print(f"Attempt {attempt + 1} failed: {e}")
            if attempt < max_retries - 1:
                print("Retrying in 2 seconds...")
                time.sleep(2)

    if not success:
        print("All retries failed due to server load.")
        try:
            esp32.write(b"102\n")  # Default to high risk if all retries fail
        except Exception:
            pass

def send_sms_alert():
    # Uses the global TWILIO_ACCOUNT_SID and TWILIO_AUTH_TOKEN defined at the top of your script
    client = Client(TWILIO_ACCOUNT_SID, TWILIO_AUTH_TOKEN)

    try: 
        print("Connecting to Twilio...")
        # Using only the approved template string for unverified trial accounts
        message = client.messages.create(
            body="sms_account_alerts",
            # PERSONAL DATA HIDDEN FOR PRIVACY REASONS
            from_="+000000000",
            to="+0000000000"
        )
        print("SMS sent successfully! SID:", message.sid)
    except Exception as e:
        print(f"Error sending SMS: {e}")

print("Listening for ESP32 button press...")

while True:
    try:
        if esp32 is not None and esp32.in_waiting > 0:
            message = esp32.readline().decode('utf-8', errors='ignore').strip()
            print(f"Received from ESP32: '{message}'")  # Debug print
            
            if message == "SNAP":
                analyze_screen()
            elif message == "NOTIFY: Threat detected. Alerting family!":
                send_sms_alert()
                
    except (SerialException, OSError) as e:
        print(f"Connection lost: {e}. Attempting to reconnect...")
        esp32 = None
        while esp32 is None:
            try:
                esp32 = serial.Serial(port, baud, timeout=1)
                print(f"Reconnected to {port}")
            except SerialException:
                time.sleep(2)
    time.sleep(0.1)