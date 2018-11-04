
package main
/**
* ESP32
*
*/
import (
    "encoding/json"
    "strconv"
    "time"
    "fmt"
    "io/ioutil"
    "log"
    "net/http"
    "os"
    "html/template"
)
//----------------------------
type Response struct {
    Raw Raw `json:"raw"`
    Reading Reading `json:"reading"`
    Schedule Schedule `json:"schedule"`
    Plant Plant `json:"plant"`
}
type Raw struct {
    Raw_soil_moist_value int `json:"raw_soil_moist_value"`
    Raw_photo_value int `json:"raw_photo_value"`
    Raw_motion_value int `json:"raw_motion_value"`
}
type Reading struct {
    Tempc int `json:"tempc"`
    Tempf int `json:"tempf"`
    Humidity int `json:"humidity"`
    Soil_moisture_level string `json:"soil_moisture_level"`
    Light_level string `json:"light_level"`
    Motion_sensor_obstructed bool `json:"motion_sensor_obstructed"`
}
type Schedule struct {
    Day0 Day0 `json:"day0"`
    Day1 Day1 `json:"day1"`
    Day2 Day2 `json:"day2"`
}
type Day0 struct {
    Day0_water_amount string `json:"day0_water_amount"`
    Day0_shade_amount string `json:"day0_shade_amount"`
}
type Day1 struct {
    Day1_water_amount string `json:"day1_water_amount"`
    Day1_shade_amount string `json:"day1_shade_amount"`
}
type Day2 struct {
    Day2_water_amount string `json:"day2_water_amount"`
    Day2_shade_amount string `json:"day2_shade_amount"`
}
type Plant struct {
    Plant_name string `json:"plant_name"`
    Plant_optimal_sun string `json:"plant_optimal_sun"`
    Plant_optimal_water string `json:"plant_optimal_water"`
}
//----------------------------
type TodoPageData struct {
    PageTitle string
    Todos []Todo
}
type Todo struct {
    Title string
    Done bool
}
//----------------------------
func main() {
    //-----------------------------------------------
    // HTML templte
    http.HandleFunc("/", handler)
    http.ListenAndServe(":8080", nil)
}
func handler(w http.ResponseWriter, r *http.Request) {
    // fmt.Fprintf(w, "Hello World!")
    data := TodoPageData{
        PageTitle: "ESP32 data sampling",
        Todos: []Todo{
            {Title: "Sensor 1", Done: false},
            {Title: "Sensor 2", Done: true},
            {Title: "Sensor 3", Done: true},
        },
    }
    tmpl := template.Must(template.ParseFiles("layout.html"))
    tmpl.Execute(w, data)
    // Read in JSON
    response, err := http.Get("https://api.myjson.com/bins/1e6vy6/")
    if err != nil {
        fmt.Print(err.Error())
        os.Exit(1)
    }
    responseData, err := ioutil.ReadAll(response.Body)
    if err != nil {
        log.Fatal(err)
    }
    // fmt.Printf(string(responseData))
    var responseObject Response
    json.Unmarshal(responseData, &responseObject)
    // fmt.Printf("raw_soil_moist_value: " + strconv.Itoa(responseObject.Raw.Raw_soil_moist_value))
    // fmt.Printf("tempf: " + strconv.Itoa(responseObject.Reading.Tempf))
    fmt.Fprintf(w, "<head>")
    fmt.Fprintf(w, "<meta http-equiv='refresh' content='1'>")
    fmt.Fprintf(w, "</head>")
    fmt.Fprintf(w, "<hr>")
    fmt.Fprintf(w, "<h3>" + time.Now().UTC().String() + "</h3>")
    fmt.Fprintf(w, "<hr>")
    fmt.Fprintf(w, "<h2>Plant: " + responseObject.Plant.Plant_name + "</h2>")
    fmt.Fprintf(w, "<h4>Sunlight Required: " + responseObject.Plant.Plant_optimal_sun + "</h4>")
    fmt.Fprintf(w, "<h4>Water Required: " + responseObject.Plant.Plant_optimal_water + "</h4>")
    fmt.Fprintf(w, "<form action='' method='put'>")
    fmt.Fprintf(w, "Switch Plant: <input type='text' name='new_plant_name'>")
    fmt.Fprintf(w, "</form>")
    fmt.Fprintf(w, "<hr>")
    fmt.Fprintf(w, "<table border='1' style='width:100%' cellpadding='5pt'>")
    fmt.Fprintf(w, "<caption>Watering System Schedule</caption>")
    fmt.Fprintf(w, "<tr>")
    fmt.Fprintf(w, "    <th></th>")
    fmt.Fprintf(w, "    <th>Today</th>")
    fmt.Fprintf(w, "    <th>Tomorrow</th>")
    fmt.Fprintf(w, "    <th>Day After Tomorrow</th>")
    fmt.Fprintf(w, "</tr>")
    fmt.Fprintf(w, "<tr>")
    fmt.Fprintf(w, "    <td>")
    fmt.Fprintf(w, "Water Amount")
    fmt.Fprintf(w, "    </td>")
    fmt.Fprintf(w, "    <td>")
    fmt.Fprintf(w, responseObject.Schedule.Day0.Day0_water_amount)
    fmt.Fprintf(w, "    </td>")
    fmt.Fprintf(w, "    <td>")
    fmt.Fprintf(w, responseObject.Schedule.Day1.Day1_water_amount)
    fmt.Fprintf(w, "    </td>")
    fmt.Fprintf(w, "    <td>")
    fmt.Fprintf(w, responseObject.Schedule.Day2.Day2_water_amount)
    fmt.Fprintf(w, "    </td>")
    fmt.Fprintf(w, "</tr>")
    fmt.Fprintf(w, "<tr>")
    fmt.Fprintf(w, "    <td>")
    fmt.Fprintf(w, "Shade Amount")
    fmt.Fprintf(w, "    </td>")
    fmt.Fprintf(w, "    <td>")
    fmt.Fprintf(w, responseObject.Schedule.Day0.Day0_shade_amount)
    fmt.Fprintf(w, "    </td>")
    fmt.Fprintf(w, "    <td>")
    fmt.Fprintf(w, responseObject.Schedule.Day1.Day1_shade_amount)
    fmt.Fprintf(w, "    </td>")
    fmt.Fprintf(w, "    <td>")
    fmt.Fprintf(w, responseObject.Schedule.Day2.Day2_shade_amount)
    fmt.Fprintf(w, "    </td>")
    fmt.Fprintf(w, "</tr>")
    fmt.Fprintf(w, "</table>")
    fmt.Fprintf(w, "<h4>Light Level: " + responseObject.Reading.Light_level + "</h4>")
    fmt.Fprintf(w, "<h4>Soil Moisture Level: " + responseObject.Reading.Soil_moisture_level + "</h4>")
    fmt.Fprintf(w, "<h4>Motion Sensor Obstructed: " + "<font color='red'>" + strconv.FormatBool(responseObject.Reading.Motion_sensor_obstructed) + "</font></h4>")
    fmt.Fprintf(w, "<form action='' method='put'>")
    fmt.Fprintf(w, "<button type='submit' name='put_motion' value='reset_put_motion'>Reset Motion Sensor</button>")
    fmt.Fprintf(w, "</form>")
    fmt.Fprintf(w, "<br>")
    fmt.Fprintf(w, "<p>Humidity (%%): " + strconv.Itoa(responseObject.Reading.Humidity))
    fmt.Fprintf(w, "<p>Temperature in C: " + strconv.Itoa(responseObject.Reading.Tempc))
    fmt.Fprintf(w, "<p>Temperature in F: " + strconv.Itoa(responseObject.Reading.Tempf))
    fmt.Fprintf(w, "<br>")
    fmt.Fprintf(w, "<p>Raw soil moisture value: " + strconv.Itoa(responseObject.Raw.Raw_soil_moist_value))
    fmt.Fprintf(w, "<p>Raw photosensor value: " + strconv.Itoa(responseObject.Raw.Raw_photo_value))
    fmt.Fprintf(w, "<p>Raw motion sensor value: " + strconv.Itoa(responseObject.Raw.Raw_motion_value))
}