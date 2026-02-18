import discord
import json
import gspread
from oauth2client.service_account import ServiceAccountCredentials
import datetime
import re

# --- CONFIGURATION ---
DISCORD_TOKEN = "YOUR_DISCORD_BOT_TOKEN_HERE"
# The ID of the #data-log channel (Double check this ID in Discord)
LOGGER_CHANNEL_ID = 1473757630676598930 

# The exact name of your Google Sheet
SHEET_NAME = "MK-12 Logistics Database"

# --- GOOGLE SHEETS SETUP ---
scope = [
    "https://spreadsheets.google.com/feeds",
    "https://www.googleapis.com/auth/drive"
]

creds = ServiceAccountCredentials.from_json_keyfile_name("credentials.json", scope)
client = gspread.authorize(creds)
sheet = client.open(SHEET_NAME).sheet1

# Ensure headers exist if the sheet is empty
if not sheet.get_all_values():
    sheet.append_row(["Log ID", "Timestamp", "Type", "Item Name", "Action", "Location", "System Sync"])

# --- DISCORD BOT SETUP ---
intents = discord.Intents.default()
intents.message_content = True  
bot = discord.Client(intents=intents)

@bot.event
async def on_ready():
    print(f"🤖 MK-12 ARCHIVER ACTIVE")
    print(f"📡 Watching Channel ID: {LOGGER_CHANNEL_ID}")
    print(f"📊 Syncing to Google Sheet: {SHEET_NAME}")
    print(f"--- Listening for Webhooks and Messages ---")

@bot.event
async def on_message(message):
    # Process messages in the logger channel
    # We also check if it's a webhook because your ESP32 uses a webhook!
    if message.channel.id != LOGGER_CHANNEL_ID:
        return

    # Don't ignore webhooks (The "LOGGER APP" in your screenshot is a webhook)
    print(f"📩 New Message Detected from: {message.author} (Webhook: {message.webhook_id is not None})")

    content = message.content.strip()
    
    # Logic: Search for JSON pattern {...}
    json_str = ""
    match = re.search(r'(\{.*\}|\[.*\])', content, re.DOTALL)
    if match:
        json_str = match.group(0)

    if json_str:
        try:
            # Clean up smart quotes or extra spaces that often break JSON
            json_str = json_str.replace('“', '"').replace('”', '"')
            data = json.loads(json_str)
            print(f"🔍 Valid JSON found! Parsing data...")

            # Prepare row for Google Sheets
            row = [
                data.get("log_id", "N/A"),
                data.get("timestamp", "N/A"),
                data.get("type", "N/A"),
                data.get("item_name", "N/A"),
                data.get("action", "N/A"),
                data.get("location", "N/A"),
                datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S") 
            ]

            # Push to Google Sheets
            sheet.append_row(row)
            print(f"✅ SUCCESS: Data for '{data.get('item_name', 'Unknown')}' pushed to Sheet!")
            
            # Try to add a reaction (might fail on webhooks depending on permissions)
            try:
                await message.add_reaction("📊")
            except:
                pass

        except json.JSONDecodeError as e:
            print(f"❌ JSON Format Error: {e}")
        except Exception as e:
            print(f"❌ System Error: {e}")
    else:
        print("❓ No JSON data found in this message.")

bot.run(DISCORD_TOKEN)