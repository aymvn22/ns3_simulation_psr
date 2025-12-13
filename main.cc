/*
 * Simulacion Red Escolar con QoS y Topologia Especifica
 * NS-3 Script
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/packet-sink.h"

#include <fstream>
#include <iomanip>
#include <iostream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("SchoolNetworkSim");

// Clase principal que encapsula toda la simulacion
class SchoolNetwork {
private:
    // --- Contenedores de Nodos ---
    NodeContainer m_estudiantesNodes; // 15 aulas
    NodeContainer m_profesoresNodes;  // 15 aulas
    NodeContainer m_informaticaNodes; // 2 aulas
    NodeContainer m_adminNodes;       // 1 nodo
    NodeContainer m_invitadosNodes;   // 10 APs
    
    // Infraestructura
    NodeContainer m_routerNode;       // Router central / Switch L3
    NodeContainer m_internetNode;     // Servidor destino en Internet

    // --- Direcciones ---
    Ipv4Address m_serverAddress;      // IP del servidor en Internet

    // --- Punteros a Sinks (Receptores) para estadisticas ---
    Ptr<PacketSink> m_sinkCbr;     // Puerto 9000 (Video Alta Prioridad)
    Ptr<PacketSink> m_sinkBursty;  // Puerto 9001 (Datos Prioridad Media)
    Ptr<PacketSink> m_sinkWebBg;   // Puerto 80   (Web Fondo Baja Prioridad)
    Ptr<PacketSink> m_sinkWebStd;  // Puerto 8080 (Web Normal)

    // --- Archivo para datos de graficas ---
    std::ofstream m_throughputStream;

public:
    // Constructor: Inicializa nodos y abre archivos
    SchoolNetwork() {
        // 1. Crear Nodos
        m_estudiantesNodes.Create(15);
        m_profesoresNodes.Create(15);
        m_informaticaNodes.Create(2);
        m_adminNodes.Create(1);
        m_invitadosNodes.Create(10);
        
        m_routerNode.Create(1);
        m_internetNode.Create(1);

        // Nombres para el sistema de logs
        Names::Add("Router", m_routerNode.Get(0));
        Names::Add("Internet", m_internetNode.Get(0));
        
        // Preparar archivo de datos
        m_throughputStream.open("throughput-data.dat");
        m_throughputStream << "# Time(s) \t CBR(Mbps) \t Bursty(Mbps) \t WebStd(Mbps)" << std::endl;
    }

    // Destructor: Cierra archivos
    ~SchoolNetwork() {
        if (m_throughputStream.is_open()) {
            m_throughputStream.close();
        }
    }

    // --- METODO PRINCIPAL DE EJECUCION ---
    void Run() {
        NS_LOG_INFO("Configurando Stack TCP/IP...");
        InstallInternetStack();
        
        NS_LOG_INFO("Configurando Topologia y Direcciones...");
        SetupTopology();
        
        // REPORTE DE IPs (Solicitado)
        PrintSpecificIPs();

        NS_LOG_INFO("Configurando Aplicaciones y Trafico...");
        SetupApplications();

        // Configurar monitoreo (Callback cada 0.5s)
        Simulator::Schedule(Seconds(1.0), &SchoolNetwork::MonitorThroughput, this);

        // Habilitar PCAP en el Router para verificar ToS con Wireshark
        // Generara el archivo: school-router-1.pcap
   //     CsmaHelper csma;
 //       csma.EnablePcap("school-router", m_routerNode.Get(0)->GetDevice(1), true);

		PointToPointHelper p2pPcap;
		p2pPcap.EnablePcap("server-internet", m_internetNode.Get(0)->GetDevice(1), true);

        NS_LOG_INFO("Iniciando Simulacion (duracion 12s)...");
        Simulator::Stop(Seconds(12.0));
        Simulator::Run();
        
        // Estadisticas finales en consola
        PrintFinalStats();
        
        Simulator::Destroy();
        NS_LOG_INFO("Simulacion Finalizada.");
    }

private:

    // Instalar protocolo Internet (IPv4)
    void InstallInternetStack() {
        InternetStackHelper stack;
        stack.SetIpv6StackInstall(false); // Desactivar IPv6

        stack.Install(m_estudiantesNodes);
        stack.Install(m_profesoresNodes);
        stack.Install(m_informaticaNodes);
        stack.Install(m_adminNodes);
        stack.Install(m_invitadosNodes);
        stack.Install(m_routerNode);
        stack.Install(m_internetNode);
    }

    // Configurar canales CSMA y asignar IPs
    void SetupTopology() {
        CsmaHelper csma;
        csma.SetChannelAttribute("DataRate", StringValue("1Gbps"));
        csma.SetChannelAttribute("Delay", StringValue("2ms"));

        Ipv4AddressHelper ipv4;

        // --- SUBRED 1: ADMINISTRACION (192.168.10.x) ---
        NodeContainer netAdmin = NodeContainer(m_routerNode.Get(0), m_adminNodes);
        NetDeviceContainer devAdmin = csma.Install(netAdmin);
        ipv4.SetBase("192.168.10.0", "255.255.255.0");
        ipv4.Assign(devAdmin);

        // --- SUBRED 2: PROFESORES (192.168.20.x) ---
        NodeContainer netProfs;
        netProfs.Add(m_routerNode.Get(0));
        netProfs.Add(m_profesoresNodes);
        NetDeviceContainer devProfs = csma.Install(netProfs);
        ipv4.SetBase("192.168.20.0", "255.255.255.0");
        ipv4.Assign(devProfs);

        // --- SUBRED 3: ESTUDIANTES + INFORMATICA (192.168.30.x) ---
        // Comparten el mismo segmento de red fisica/logica
        NodeContainer netAlumnosLabs;
        netAlumnosLabs.Add(m_routerNode.Get(0));
        netAlumnosLabs.Add(m_estudiantesNodes);
        netAlumnosLabs.Add(m_informaticaNodes);
        NetDeviceContainer devAlumnosLabs = csma.Install(netAlumnosLabs);
        ipv4.SetBase("192.168.30.0", "255.255.255.0");
        ipv4.Assign(devAlumnosLabs);

        // --- SUBRED 4: INVITADOS (192.168.40.x) ---
        NodeContainer netInvitados;
        netInvitados.Add(m_routerNode.Get(0));
        netInvitados.Add(m_invitadosNodes);
        NetDeviceContainer devInvitados = csma.Install(netInvitados);
        ipv4.SetBase("192.168.40.0", "255.255.255.0");
        ipv4.Assign(devInvitados);

        // --- ENLACE A INTERNET (P2P) ---
        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
        p2p.SetChannelAttribute("Delay", StringValue("5ms"));

        NodeContainer netInternet = NodeContainer(m_routerNode.Get(0), m_internetNode.Get(0));
        NetDeviceContainer devInternet = p2p.Install(netInternet);
        ipv4.SetBase("203.0.113.0", "255.255.255.252"); // IP Publica simulada
        Ipv4InterfaceContainer ifInternet = ipv4.Assign(devInternet);

        m_serverAddress = ifInternet.GetAddress(1); // IP del nodo Internet

        // Tablas de enrutamiento globales
        Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    }

    // Configurar Generadores (Clientes) y Receptores (Servidor)
    void SetupApplications() {
        // A. Configurar Sinks (Servidor Internet)
        // ---------------------------------------
        // UDP Port 9000: CBR Video (Alta Prioridad)
        PacketSinkHelper sinkCbrHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), 9000));
        ApplicationContainer appCbr = sinkCbrHelper.Install(m_internetNode.Get(0));
        appCbr.Start(Seconds(0.0));
        m_sinkCbr = DynamicCast<PacketSink>(appCbr.Get(0));

        // UDP Port 9001: Bursty (Media Prioridad)
        PacketSinkHelper sinkBurstyHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), 9001));
        ApplicationContainer appBursty = sinkBurstyHelper.Install(m_internetNode.Get(0));
        appBursty.Start(Seconds(0.0));
        m_sinkBursty = DynamicCast<PacketSink>(appBursty.Get(0));

        // TCP Port 80: Web Background (Baja Prioridad)
        PacketSinkHelper sinkBgHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), 80));
        ApplicationContainer appBg = sinkBgHelper.Install(m_internetNode.Get(0));
        appBg.Start(Seconds(0.0));
        m_sinkWebBg = DynamicCast<PacketSink>(appBg.Get(0));

        // TCP Port 8080: Web Standard (Normal)
        PacketSinkHelper sinkStdHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), 8080));
        ApplicationContainer appStd = sinkStdHelper.Install(m_internetNode.Get(0));
        appStd.Start(Seconds(0.0));
        m_sinkWebStd = DynamicCast<PacketSink>(appStd.Get(0));


        // B. Configurar Clientes (Generadores de Trafico)
        // -----------------------------------------------

        // 1. Estudiantes y Profesores (Bursty + CBR + WebStd)
        for (uint32_t i = 0; i < 15; ++i) {
            // Estudiantes
            CreateBursty(m_estudiantesNodes.Get(i), 1.0 + (i*0.1));
            CreateCBR(m_estudiantesNodes.Get(i),    2.0 + (i*0.1));
            CreateWebStandard(m_estudiantesNodes.Get(i), 1.0 + (i*0.2));

            // Profesores
            CreateBursty(m_profesoresNodes.Get(i),  1.5 + (i*0.1));
            CreateCBR(m_profesoresNodes.Get(i),     2.5 + (i*0.1));
            CreateWebStandard(m_profesoresNodes.Get(i), 1.5 + (i*0.2));
        }

        // 2. Invitados (Solo Web Estandar)
        for (uint32_t i = 0; i < 10; ++i) {
            CreateWebStandard(m_invitadosNodes.Get(i), 3.0 + (i*0.5));
        }

        // 3. Admin (Solo Bursty)
        CreateBursty(m_adminNodes.Get(0), 1.0);

        // 4. Informatica (Bursty + Web Fondo)
        for (uint32_t i = 0; i < 2; ++i) {
            CreateBursty(m_informaticaNodes.Get(i), 1.0);
            CreateWebBackground(m_informaticaNodes.Get(i), 0.5);
        }
    }

    // --- Helpers Privados de Creacion de Trafico con TOS ---

    void CreateCBR(Ptr<Node> node, double start) {
        // Video HD Simulado
        OnOffHelper onoff("ns3::UdpSocketFactory", Address(InetSocketAddress(m_serverAddress, 9000)));
        onoff.SetConstantRate(DataRate("2Mbps"));
        onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
        // TOS 0xB8 = DSCP EF (Expedited Forwarding) - Alta Prioridad
        onoff.SetAttribute("Tos", UintegerValue(0xB8)); 
        
        ApplicationContainer app = onoff.Install(node);
        app.Start(Seconds(start));
        app.Stop(Seconds(10.0));
    }

    void CreateBursty(Ptr<Node> node, double start) {
        // Trafico interactivo ráfagas
        OnOffHelper onoff("ns3::UdpSocketFactory", Address(InetSocketAddress(m_serverAddress, 9001)));
        onoff.SetAttribute("DataRate", StringValue("500kbps"));
        onoff.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=1.0]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=1.0]"));
        // TOS 0x48 = DSCP AF21 - Prioridad Media
        onoff.SetAttribute("Tos", UintegerValue(0x48)); 

        ApplicationContainer app = onoff.Install(node);
        app.Start(Seconds(start));
        app.Stop(Seconds(10.0));
    }

    void CreateWebBackground(Ptr<Node> node, double start) {
        // Web constante de fondo
        OnOffHelper onoff("ns3::TcpSocketFactory", Address(InetSocketAddress(m_serverAddress, 80)));
        onoff.SetConstantRate(DataRate("100kbps"));
        // TOS 0x00 = Best Effort - Prioridad Baja
        onoff.SetAttribute("Tos", UintegerValue(0x00)); 

        ApplicationContainer app = onoff.Install(node);
        app.Start(Seconds(start));
        app.Stop(Seconds(10.0));
    }

    void CreateWebStandard(Ptr<Node> node, double start) {
        // Navegacion Web normal
        OnOffHelper onoff("ns3::TcpSocketFactory", Address(InetSocketAddress(m_serverAddress, 8080)));
        onoff.SetAttribute("DataRate", StringValue("1Mbps"));
        onoff.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=2.0]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=5.0]"));
        // TOS 0x28 = DSCP AF11 - Prioridad Normal
        onoff.SetAttribute("Tos", UintegerValue(0x28)); 

        ApplicationContainer app = onoff.Install(node);
        app.Start(Seconds(start));
        app.Stop(Seconds(10.0));
    }

    // --- voir les ip de chaque noeuds---
    void PrintSpecificIPs() {
        std::cout << "\n========================================================" << std::endl;
        std::cout << "          REPORTE DE ASIGNACION DE IP" << std::endl;
        std::cout << "========================================================" << std::endl;

        auto printNodeIp = [](std::string name, Ptr<Node> n) {
            Ptr<Ipv4> ip = n->GetObject<Ipv4>();
            // Interface 1 suele ser la conectada al CSMA (0 es loopback)
            std::cout << "  - " << std::left << std::setw(20) << name 
                      << ": " << ip->GetAddress(1, 0).GetLocal() << std::endl;
        };

        std::cout << "ADMINISTRACION (Red 10.x):" << std::endl;
        printNodeIp("Nodo Admin", m_adminNodes.Get(0));

        std::cout << "\nAULAS PROFESORES (Red 20.x):" << std::endl;
        for (uint32_t i = 0; i < m_profesoresNodes.GetN(); ++i) {
            std::stringstream ss; ss << "Aula " << (i+1);
            printNodeIp(ss.str(), m_profesoresNodes.Get(i));
        }

        std::cout << "\nAULAS ALUMNOS (Red 30.x):" << std::endl;
        for (uint32_t i = 0; i < m_estudiantesNodes.GetN(); ++i) {
            std::stringstream ss; ss << "Aula " << (i+1);
            printNodeIp(ss.str(), m_estudiantesNodes.Get(i));
        }

        std::cout << "\nAULAS INFORMATICA (Red 30.x - Compartida):" << std::endl;
        for (uint32_t i = 0; i < m_informaticaNodes.GetN(); ++i) {
            std::stringstream ss; ss << "Informatica " << (i+1);
            printNodeIp(ss.str(), m_informaticaNodes.Get(i));
        }

        std::cout << "\nINVITADOS (Red 40.x):" << std::endl;
        for (uint32_t i = 0; i < m_invitadosNodes.GetN(); ++i) {
            std::stringstream ss; ss << "AP Invitado " << (i+1);
            printNodeIp(ss.str(), m_invitadosNodes.Get(i));
        }
        std::cout << "========================================================\n" << std::endl;
    }

    // --- MONITOREO Y ESTADISTICAS ---
    
    
    void MonitorThroughput() {
        double time = Simulator::Now().GetSeconds();
        
        // Calculo simple de tasa instantanea (promediada desde t=0)
        // Para graficas mas "puntiagudas" se necesitaria delta_bytes / delta_time
        double rateCbr = (m_sinkCbr->GetTotalRx() * 8.0) / 1e6 / time;
        double rateBursty = (m_sinkBursty->GetTotalRx() * 8.0) / 1e6 / time;
        double rateWebStd = (m_sinkWebStd->GetTotalRx() * 8.0) / 1e6 / time;

        m_throughputStream << time << "\t" << rateCbr << "\t" << rateBursty << "\t" << rateWebStd << std::endl;

        Simulator::Schedule(Seconds(0.5), &SchoolNetwork::MonitorThroughput, this);
    }

    // Resumen final en consola
    void PrintFinalStats() {
        std::cout << "\n--- ESTADISTICAS FINALES (SERVIDOR) ---" << std::endl;
        double totalTime = 12.0;

        auto printStat = [&](std::string name, Ptr<PacketSink> sink) {
            uint64_t totalBytes = sink->GetTotalRx();
            double throughputKbps = (totalBytes * 8.0) / 1000.0 / totalTime;
            
            std::cout << "Trafico " << std::left << std::setw(15) << name 
                      << " | RX Bytes: " << std::setw(10) << totalBytes 
                      << " | Throughput: " << std::fixed << std::setprecision(2) 
                      << throughputKbps << " Kbps" << std::endl;
        };

        printStat("CBR (Video)", m_sinkCbr);
        printStat("Bursty", m_sinkBursty);
        printStat("Web Fondo", m_sinkWebBg);
        printStat("Web Std", m_sinkWebStd);
        
        std::cout << "\n[GENERADOS] Archivos de salida:" << std::endl;
        std::cout << " 1. 'throughput-data.dat' -> Usar Gnuplot/Excel para graficas." << std::endl;
        std::cout << " 2. 'school-router-1.pcap' -> Usar Wireshark para ver Prioridad (TOS)." << std::endl;
    }
};

// --- MAIN ---
int main (int argc, char *argv[]) {
    Time::SetResolution (Time::NS);
    
    // Habilitar logs
    LogComponentEnable ("SchoolNetworkSim", LOG_LEVEL_INFO);
    
    // Instanciar y ejecutar
    SchoolNetwork escuela;
    escuela.Run();

    return 0;
}