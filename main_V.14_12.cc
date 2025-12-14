/*
 * Simulacion Red Escolar con QoS y Topologia Especifica
 * Version: Ingenieria de Telecomunicaciones (Full Verbose)
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
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h" // Modulo clave para ingenieria

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("SchoolNetworkSim");

class SchoolNetwork {
private:
    // --- Variables de Configuracion ---
    double m_simTime;

    // --- Contenedores de Nodos ---
    NodeContainer m_estudiantesNodes;
    NodeContainer m_profesoresNodes;
    NodeContainer m_informaticaNodes;
    NodeContainer m_adminNodes;
    NodeContainer m_invitadosNodes;
    
    // Infraestructura
    NodeContainer m_routerNode;
    NodeContainer m_internetNode;

    // --- Direcciones ---
    Ipv4Address m_serverAddress;

    // --- Punteros a Sinks ---
    Ptr<PacketSink> m_sinkCbr;
    Ptr<PacketSink> m_sinkBursty;
    Ptr<PacketSink> m_sinkWebBg;
    Ptr<PacketSink> m_sinkWebStd;

    // --- Archivo para datos ---
    std::ofstream m_throughputStream;

    // --- Monitor de Flujo (Ingenieria) ---
    FlowMonitorHelper m_flowMonHelper;
    Ptr<FlowMonitor> m_flowMonitor;

public:
    SchoolNetwork() {
        m_simTime = 50.0; // 300 Segundos

        // Crear Nodos
        m_estudiantesNodes.Create(15);
        m_profesoresNodes.Create(15);
        m_informaticaNodes.Create(2);
        m_adminNodes.Create(1);
        m_invitadosNodes.Create(10);
        
        m_routerNode.Create(1);
        m_internetNode.Create(1);

        Names::Add("Router", m_routerNode.Get(0));
        Names::Add("Internet", m_internetNode.Get(0));
        
        m_throughputStream.open("throughput-data.dat");
        m_throughputStream << "# Time(s) \t CBR(Mbps) \t Bursty(Mbps) \t WebStd(Mbps)" << std::endl;
    }

    ~SchoolNetwork() {
        if (m_throughputStream.is_open()) {
            m_throughputStream.close();
        }
    }

    void Run() {
        std::cout << "--- INICIO DE CONFIGURACION DE ESCENARIO ---" << std::endl;
        
        NS_LOG_INFO("Configurando Stack TCP/IP...");
        InstallInternetStack();
        
        NS_LOG_INFO("Configurando Topologia (Colas Ampliadas)...");
        SetupTopology();
        
        // REPORTE TECNICO 1: L2/L3
        PrintEngineeringReport();

        NS_LOG_INFO("Configurando Aplicaciones...");
        SetupApplications();

        // REPORTE TECNICO 2: Enrutamiento
        PrintRoutingTables();

        Simulator::Schedule(Seconds(1.0), &SchoolNetwork::MonitorThroughput, this);

        // PCAP
        PointToPointHelper p2pPcap;
        p2pPcap.EnablePcap("server-internet", m_internetNode.Get(0)->GetDevice(1), true);

        // Instalar FlowMonitor para metricas detalladas (Jitter, Delay, Packet Loss)
        m_flowMonitor = m_flowMonHelper.InstallAll();

        NS_LOG_INFO("Iniciando Simulacion (duracion " << m_simTime << "s)...");
        Simulator::Stop(Seconds(m_simTime));
        Simulator::Run();
        
        // REPORTE TECNICO 3: Estadisticas de Flujo
        PrintFlowMetrics();
        PrintFinalStats();
        
        Simulator::Destroy();
        NS_LOG_INFO("Simulacion Finalizada.");
    }

private:

    void InstallInternetStack() {
        InternetStackHelper stack;
        stack.SetIpv6StackInstall(false);

        stack.Install(m_estudiantesNodes);
        stack.Install(m_profesoresNodes);
        stack.Install(m_informaticaNodes);
        stack.Install(m_adminNodes);
        stack.Install(m_invitadosNodes);
        stack.Install(m_routerNode);
        stack.Install(m_internetNode);
    }

    void SetupTopology() {
        CsmaHelper csma;
        csma.SetChannelAttribute("DataRate", StringValue("1Gbps"));
        csma.SetChannelAttribute("Delay", StringValue("2ms"));
        // Cola aumentada para evitar drops en SYN TCP
        csma.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("10000p"));

        Ipv4AddressHelper ipv4;

        // Subredes
        NodeContainer netAdmin = NodeContainer(m_routerNode.Get(0), m_adminNodes);
        NetDeviceContainer devAdmin = csma.Install(netAdmin);
        ipv4.SetBase("192.168.10.0", "255.255.255.0");
        ipv4.Assign(devAdmin);

        NodeContainer netProfs;
        netProfs.Add(m_routerNode.Get(0));
        netProfs.Add(m_profesoresNodes);
        NetDeviceContainer devProfs = csma.Install(netProfs);
        ipv4.SetBase("192.168.20.0", "255.255.255.0");
        ipv4.Assign(devProfs);

        NodeContainer netAlumnosLabs;
        netAlumnosLabs.Add(m_routerNode.Get(0));
        netAlumnosLabs.Add(m_estudiantesNodes);
        netAlumnosLabs.Add(m_informaticaNodes);
        NetDeviceContainer devAlumnosLabs = csma.Install(netAlumnosLabs);
        ipv4.SetBase("192.168.30.0", "255.255.255.0");
        ipv4.Assign(devAlumnosLabs);

        NodeContainer netInvitados;
        netInvitados.Add(m_routerNode.Get(0));
        netInvitados.Add(m_invitadosNodes);
        NetDeviceContainer devInvitados = csma.Install(netInvitados);
        ipv4.SetBase("192.168.40.0", "255.255.255.0");
        ipv4.Assign(devInvitados);

        // Enlace Internet P2P
        PointToPointHelper p2p;
        p2p.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
        p2p.SetChannelAttribute("Delay", StringValue("5ms"));
        p2p.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("10000p"));

        // QoS Traffic Control
        TrafficControlHelper tch;
        tch.SetRootQueueDisc("ns3::PfifoFastQueueDisc", "MaxSize", StringValue("10000p"));

        NodeContainer netInternet = NodeContainer(m_routerNode.Get(0), m_internetNode.Get(0));
        NetDeviceContainer devInternet = p2p.Install(netInternet);
        
        tch.Install(devInternet);

        ipv4.SetBase("203.0.113.0", "255.255.255.252");
        Ipv4InterfaceContainer ifInternet = ipv4.Assign(devInternet);

        m_serverAddress = ifInternet.GetAddress(1);

        Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    }

    void SetupApplications() {
        // A. Sinks
        PacketSinkHelper sinkCbrHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), 9000));
        ApplicationContainer appCbr = sinkCbrHelper.Install(m_internetNode.Get(0));
        appCbr.Start(Seconds(0.0));
        m_sinkCbr = DynamicCast<PacketSink>(appCbr.Get(0));

        PacketSinkHelper sinkBurstyHelper("ns3::UdpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), 9001));
        ApplicationContainer appBursty = sinkBurstyHelper.Install(m_internetNode.Get(0));
        appBursty.Start(Seconds(0.0));
        m_sinkBursty = DynamicCast<PacketSink>(appBursty.Get(0));

        PacketSinkHelper sinkBgHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), 80));
        ApplicationContainer appBg = sinkBgHelper.Install(m_internetNode.Get(0));
        appBg.Start(Seconds(0.0));
        m_sinkWebBg = DynamicCast<PacketSink>(appBg.Get(0));

        PacketSinkHelper sinkStdHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), 8080));
        ApplicationContainer appStd = sinkStdHelper.Install(m_internetNode.Get(0));
        appStd.Start(Seconds(0.0));
        m_sinkWebStd = DynamicCast<PacketSink>(appStd.Get(0));

        // B. Clientes
        for (uint32_t i = 0; i < 15; ++i) {
            CreateBursty(m_estudiantesNodes.Get(i), 1.0 + (i*0.1));
            CreateCBR(m_estudiantesNodes.Get(i),    2.0 + (i*0.1));
            CreateWebStandard(m_estudiantesNodes.Get(i), 1.0 + (i*0.2));

            CreateBursty(m_profesoresNodes.Get(i),  1.5 + (i*0.1));
            CreateCBR(m_profesoresNodes.Get(i),     2.5 + (i*0.1));
            CreateWebStandard(m_profesoresNodes.Get(i), 1.5 + (i*0.2));
        }

        for (uint32_t i = 0; i < 10; ++i) {
            CreateWebStandard(m_invitadosNodes.Get(i), 3.0 + (i*0.5));
        }

        CreateBursty(m_adminNodes.Get(0), 1.0);

        for (uint32_t i = 0; i < 2; ++i) {
            CreateBursty(m_informaticaNodes.Get(i), 1.0);
            CreateWebBackground(m_informaticaNodes.Get(i), 0.5);
        }
    }

    void CreateCBR(Ptr<Node> node, double start) {
        OnOffHelper onoff("ns3::UdpSocketFactory", Address(InetSocketAddress(m_serverAddress, 9000)));
        onoff.SetConstantRate(DataRate("2Mbps"));
        onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
        onoff.SetAttribute("Tos", UintegerValue(0xB8)); 
        ApplicationContainer app = onoff.Install(node);
        app.Start(Seconds(start));
        app.Stop(Seconds(m_simTime - 1.0)); 
    }

    void CreateBursty(Ptr<Node> node, double start) {
        OnOffHelper onoff("ns3::UdpSocketFactory", Address(InetSocketAddress(m_serverAddress, 9001)));
        onoff.SetAttribute("DataRate", StringValue("500kbps"));
        onoff.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=1.0]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=1.0]"));
        onoff.SetAttribute("Tos", UintegerValue(0x48));
        ApplicationContainer app = onoff.Install(node);
        app.Start(Seconds(start));
        app.Stop(Seconds(m_simTime - 1.0));
    }

    void CreateWebBackground(Ptr<Node> node, double start) {
        OnOffHelper onoff("ns3::TcpSocketFactory", Address(InetSocketAddress(m_serverAddress, 80)));
        onoff.SetConstantRate(DataRate("100kbps"));
        onoff.SetAttribute("Tos", UintegerValue(0x00));
        ApplicationContainer app = onoff.Install(node);
        app.Start(Seconds(start));
        app.Stop(Seconds(m_simTime - 1.0));
    }

    void CreateWebStandard(Ptr<Node> node, double start) {
        OnOffHelper onoff("ns3::TcpSocketFactory", Address(InetSocketAddress(m_serverAddress, 8080)));
        onoff.SetAttribute("DataRate", StringValue("1Mbps"));
        onoff.SetAttribute("OnTime", StringValue("ns3::ExponentialRandomVariable[Mean=2.0]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ExponentialRandomVariable[Mean=5.0]"));
        onoff.SetAttribute("Tos", UintegerValue(0x28));
        ApplicationContainer app = onoff.Install(node);
        app.Start(Seconds(start));
        app.Stop(Seconds(m_simTime - 1.0));
    }

    // --- REPORTE DE INGENIERIA 1: DIRECCIONAMIENTO L2/L3 ---
    void PrintEngineeringReport() {
        std::cout << "\n################################################################" << std::endl;
        std::cout << "              TOPOLOGIA Y DIRECCIONAMIENTO" << std::endl;
        std::cout << "################################################################" << std::endl;
        
        auto printDetails = [](std::string context, Ptr<Node> n) {
            std::cout << "[" << context << "] Node ID: " << n->GetId() << std::endl;
            Ptr<Ipv4> ipv4 = n->GetObject<Ipv4>();
            int32_t nIfaces = ipv4->GetNInterfaces();
            
            for (int32_t i = 0; i < nIfaces; i++) {
                if (ipv4->IsUp(i)) { // Solo interfaces activas
                    Ipv4Address ip = ipv4->GetAddress(i, 0).GetLocal();
                    if (ip == Ipv4Address::GetLoopback()) continue; // Ignorar loopback

                    Ptr<NetDevice> dev = n->GetDevice(i);
                    Address mac = dev->GetAddress();
                    
                    std::cout << "   |-- Interface " << i 
                              << " (" << (DynamicCast<CsmaNetDevice>(dev) ? "CSMA" : "P2P/Other") << ")" << std::endl;
                    std::cout << "       |-- MAC Addr: " << mac << std::endl;
                    std::cout << "       |-- IP  Addr: " << ip << " / " << ipv4->GetAddress(i, 0).GetMask() << std::endl;
                    std::cout << "       |-- MTU     : " << dev->GetMtu() << " bytes" << std::endl;
                }
            }
            std::cout << "------------------------------------------------------------" << std::endl;
        };

        std::cout << "\n>>> INFRAESTRUCTURA CENTRAL (CORE/WAN) <<<" << std::endl;
        printDetails("ROUTER PRINCIPAL", m_routerNode.Get(0));
        printDetails("SERVIDOR INTERNET", m_internetNode.Get(0));

        std::cout << "\n>>> SUBREDES DE ACCESO (MUESTREO) <<<" << std::endl;
        printDetails("VLAN 10 - ADMIN", m_adminNodes.Get(0));
        printDetails("VLAN 20 - PROFESOR (Ejemplo 1)", m_profesoresNodes.Get(0));
        printDetails("VLAN 30 - ESTUDIANTE (Ejemplo 1)", m_estudiantesNodes.Get(0));
        printDetails("VLAN 40 - INVITADO (Ejemplo 1)", m_invitadosNodes.Get(0));
    }

    // --- REPORTE DE INGENIERIA 2: TABLAS DE ENRUTAMIENTO ---
    void PrintRoutingTables() {
        std::cout << "\n################################################################" << std::endl;
        std::cout << "              TABLAS DE ENRUTAMIENTO (IPv4)" << std::endl;
        std::cout << "################################################################" << std::endl;
        
        // Imprimir rutas del Router
        Ptr<OutputStreamWrapper> routingStream = Create<OutputStreamWrapper> (&std::cout);
        Ipv4GlobalRoutingHelper::PrintRoutingTableAt(Seconds(0.1), m_routerNode.Get(0), routingStream);
    }

    void MonitorThroughput() {
        double time = Simulator::Now().GetSeconds();
        double rateCbr = (m_sinkCbr->GetTotalRx() * 8.0) / 1e6 / time; 
        double rateBursty = (m_sinkBursty->GetTotalRx() * 8.0) / 1e6 / time;
        double rateWebStd = (m_sinkWebStd->GetTotalRx() * 8.0) / 1e6 / time;

        m_throughputStream << time << "\t" << rateCbr << "\t" << rateBursty << "\t" << rateWebStd << std::endl;
        Simulator::Schedule(Seconds(0.5), &SchoolNetwork::MonitorThroughput, this);
    }

    // --- REPORTE DE INGENIERIA 3: METRICAS DE FLUJO ---
    void PrintFlowMetrics() {
        std::cout << "\n################################################################" << std::endl;
        std::cout << "              METRICAS DE FLUJO (QoS)" << std::endl;
        std::cout << "################################################################" << std::endl;

        m_flowMonitor->CheckForLostPackets();
        Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(m_flowMonHelper.GetClassifier());
        std::map<FlowId, FlowMonitor::FlowStats> stats = m_flowMonitor->GetFlowStats();

        // Cabecera de la tabla
        std::cout << std::left << std::setw(10) << "Flow ID" 
                  << std::setw(20) << "Source" 
                  << std::setw(20) << "Destination" 
                  << std::setw(10) << "Proto" 
                  << std::setw(12) << "Tx Pkts" 
                  << std::setw(12) << "Rx Pkts" 
                  << std::setw(12) << "Lost" 
                  << std::setw(12) << "Delay(ms)" 
                  << std::setw(12) << "Jitter(ms)" << std::endl;
        
        std::cout << std::string(120, '-') << std::endl;

        for (auto const& map : stats) {
            // Filtrar flujos con pocos paquetes (ruido ARP/ICMP)
            if(map.second.txPackets < 10) continue; 

            Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(map.first);
            
            std::stringstream srcStream; srcStream << t.sourceAddress << ":" << t.sourcePort;
            std::stringstream dstStream; dstStream << t.destinationAddress << ":" << t.destinationPort;
            
            std::string proto = (t.protocol == 6) ? "TCP" : ((t.protocol == 17) ? "UDP" : "Other");

            std::cout << std::left << std::setw(10) << map.first
                      << std::setw(20) << srcStream.str()
                      << std::setw(20) << dstStream.str()
                      << std::setw(10) << proto
                      << std::setw(12) << map.second.txPackets
                      << std::setw(12) << map.second.rxPackets
                      << std::setw(12) << map.second.lostPackets
                      << std::setw(12) << (map.second.rxPackets > 0 ? (map.second.delaySum.GetSeconds() / map.second.rxPackets) * 1000 : 0)
                      << std::setw(12) << (map.second.rxPackets > 0 ? (map.second.jitterSum.GetSeconds() / map.second.rxPackets) * 1000 : 0)
                      << std::endl;
        }
    }

    void PrintFinalStats() {
        std::cout << "\n################################################################" << std::endl;
        std::cout << "              RESUMEN SERVIDOR" << std::endl;
        std::cout << "################################################################" << std::endl;
        
        std::cout << "Tiempo Total de Simulacion: " << m_simTime << " s" << std::endl;
        
        auto printStat = [&](std::string name, Ptr<PacketSink> sink) {
            uint64_t totalBytes = sink->GetTotalRx();
            double throughputKbps = (totalBytes * 8.0) / 1000.0 / m_simTime;
            
            std::cout << "Trafico " << std::left << std::setw(15) << name 
                      << " | RX Bytes: " << std::setw(10) << totalBytes 
                      << " | Throughput Promedio: " << std::fixed << std::setprecision(2) 
                      << throughputKbps << " Kbps" << std::endl;
        };

        printStat("CBR (Video)", m_sinkCbr);
        printStat("Bursty", m_sinkBursty);
        printStat("Web Fondo", m_sinkWebBg);
        printStat("Web Std", m_sinkWebStd);
        
        std::cout << "\n[ARCHIVOS GENERADOS]" << std::endl;
        std::cout << " 1. 'throughput-data.dat' -> Datos crudos para graficar ancho de banda." << std::endl;
        std::cout << " 2. 'server-internet-1.pcap' -> Analisis profundo de paquetes (Wireshark)." << std::endl;
        std::cout << " 3. 'SchoolNetworkSim.xml' -> Datos brutos del FlowMonitor." << std::endl;
        
        // Exportar FlowMonitor a XML
        m_flowMonitor->SerializeToXmlFile("SchoolNetworkSim.xml", true, true);
    }
};

int main (int argc, char *argv[]) {
    Time::SetResolution (Time::NS);
    LogComponentEnable ("SchoolNetworkSim", LOG_LEVEL_INFO);
    SchoolNetwork escuela;
    escuela.Run();
    return 0;
}
