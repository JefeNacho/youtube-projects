import psutil
import requests
import json
import time

class WeatherFetcher:
    def __init__(self, api_key, city="Monterrey"):
        self.api_key = api_key
        self.city = city
        self.base_url = "https://api.openweathermap.org/data/2.5/"

    def get_atmos_data(self):
        try:
            # Weather call
            w_url = f"{self.base_url}weather?q={self.city}&appid={self.api_key}&units=metric"
            resp = requests.get(w_url, timeout=5).json()
            
            if 'coord' not in resp:
                if 'message' in resp:
                    print(f"[WARN] Weather API Error: {resp['message']}")
                return None

            # Pollution call
            lat, lon = resp['coord']['lat'], resp['coord']['lon']
            p_url = f"{self.base_url}air_pollution?lat={lat}&lon={lon}&appid={self.api_key}"
            p_resp = requests.get(p_url, timeout=5).json()
            
            aqi = 1
            if 'list' in p_resp and len(p_resp['list']) > 0:
                aqi = p_resp['list'][0]['main']['aqi']

            return {
                "temp": resp.get('main', {}).get('temp', 0),
                "hum": resp.get('main', {}).get('humidity', 0),
                "aqi": aqi, 
                "weather_id": resp.get('weather', [{}])[0].get('id', 800)
            }
        except Exception as e:
            print(f"[ERR] Weather Fetcher Detail: {e}")
            return None

class SystemFetcher:
    def get_metrics(self):
        try:
            return {
                "cpu": psutil.cpu_percent(),
                "ram": psutil.virtual_memory().percent,
                "disk": psutil.disk_usage('/').percent
            }
        except Exception as e:
            print(f"[ERR] System Fetcher: {e}")
            return None
